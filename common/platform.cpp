#include "platform.h"
#include <algorithm>
#include <cwctype>
#include <map>

namespace {
    BOOL  (WINAPI * ptrProcessIdToSessionId) (DWORD, DWORD *) = NULL;
    DWORD (WINAPI * ptrGetActiveProcessorCount) (WORD) = NULL;

    BOOL (WINAPI * ptrSetThreadGroupAffinity) (HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY) = NULL;
    BOOL (WINAPI * ptrSetThreadIdealProcessorEx) (HANDLE, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER) = NULL;
    BOOL (WINAPI * ptrGetLogicalProcessorInformationEx) (LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD) = NULL;

    template <typename T>
    bool IsMoreThanOneBitSet (T value) {
        return value & (value - 1);
    }
    template <typename T>
    unsigned int CountSetBits (T value) {
        auto n = 0u;
        while (value) {
            n += value & 1;
            value >>= 1;
        }
        return n;
    }

    std::vector <std::uint8_t> CallGetLogicalProcessorInformationEx (LOGICAL_PROCESSOR_RELATIONSHIP relationship) {
        if (ptrGetLogicalProcessorInformationEx) {

            DWORD n = 64 * GetLogicalProcessorCount ();
            std::vector <std::uint8_t> buffer (n);

            if (ptrGetLogicalProcessorInformationEx (relationship, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) buffer.data (), &n)) {
                return buffer;

            } else
            if (GetLastError () == ERROR_INSUFFICIENT_BUFFER) {

                n += 128;
                buffer.resize (n);
                
                if (ptrGetLogicalProcessorInformationEx (relationship, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) buffer.data (), &n)) {
                    return buffer;
                }
            }
        }
        return {};
    }
}

void InitPlatformAPI () {
    if (auto hKernel32 = GetModuleHandle (L"KERNEL32")) {
        Symbol (hKernel32, ptrProcessIdToSessionId, "ProcessIdToSessionId"); // NT 5.2+
        Symbol (hKernel32, ptrGetActiveProcessorCount, "GetActiveProcessorCount"); // NT 6.1+

        Symbol (hKernel32, ptrSetThreadGroupAffinity, "SetThreadGroupAffinity"); // NT 6.1+
        Symbol (hKernel32, ptrSetThreadIdealProcessorEx, "SetThreadIdealProcessorEx"); // NT 6.1+
        Symbol (hKernel32, ptrGetLogicalProcessorInformationEx, "GetLogicalProcessorInformationEx"); // NT 6.1+
    }
}

DWORD GetLogicalProcessorCount () {
    if (ptrGetActiveProcessorCount) {
        auto n = ptrGetActiveProcessorCount (ALL_PROCESSOR_GROUPS);
        if (n)
            return n;
    }

    SYSTEM_INFO si;
    GetSystemInfo (&si);

    if (si.dwNumberOfProcessors < 1) {
        si.dwNumberOfProcessors = 1;
    }
    return si.dwNumberOfProcessors;
}

BOOL SetThreadIdealLogicalProcessor (HANDLE hThread, Processor processor) {
    if (ptrSetThreadIdealProcessorEx) {
        PROCESSOR_NUMBER number = { processor.group, processor.number };
        return ptrSetThreadIdealProcessorEx (hThread, &number, NULL);
    } else {
        return SetThreadIdealProcessor (hThread, processor.number);
    }
}

BOOL AssignThreadLogicalProcessor (HANDLE hThread, Processor processor) {
    bool affinity;
    if (ptrSetThreadGroupAffinity) {
        GROUP_AFFINITY group = { processor.affinity, processor.group, { 0,0,0 }};
        affinity = ptrSetThreadGroupAffinity (hThread, &group, NULL);
    } else {
        affinity = SetThreadAffinityMask (hThread, processor.affinity);
    }

    bool ideal = SetThreadIdealLogicalProcessor (hThread, processor);
    return ideal && affinity;
}

std::size_t GetPredominantSMT () {
    auto buffer = CallGetLogicalProcessorInformationEx (RelationProcessorCore);
    if (!buffer.empty ()) {

        unsigned int smt [256] = {};
        auto p = buffer.data ();
        while (true) {

            auto info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) p;
            if (info->Size) {
                if (info->Relationship == RelationProcessorCore) {
                    ++smt [CountSetBits (info->Processor.GroupMask [0].Mask) - 1];
                }
                p += info->Size;
            } else
                break;
        }

        unsigned int highest = 0;
        for (auto i = 1u; i != 256; ++i) {
            if (smt [highest] < smt [i]) {
                highest = i;
            }
        }
        return highest + 1;
    } else
        return 1;
}

std::vector <Processor> GetRankedLogicalProcessorList () {
    std::map <WORD, UCHAR> numbers;
    std::vector <Processor> processors;

    processors.reserve (GetLogicalProcessorCount ());

    if (ptrGetLogicalProcessorInformationEx) {

        auto buffer = CallGetLogicalProcessorInformationEx (RelationAll);
        if (!buffer.empty ()) {
            auto p = buffer.data ();

            // find highest efficiency class

            BYTE max_efficiency = 0;

            while (true) {
                auto info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) p;
                if (info->Size) {
                    if (info->Relationship == RelationProcessorCore) {
                        if (max_efficiency < info->Processor.EfficiencyClass) {
                            max_efficiency = info->Processor.EfficiencyClass;
                        }
                    }
                    p += info->Size;
                } else
                    break;
            }

            // create logical processor entries

            Processor processor;

            p = buffer.data ();
            while (true) {
                auto info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) p;
                if (info->Size) {
                    if (info->Relationship == RelationProcessorCore) {

                        processor.group = info->Processor.GroupMask [0].Group;
                        processor.affinity = info->Processor.GroupMask [0].Mask;
                        processor.smt = 0;

                        // invert efficiency class
                        //  - API returns 1 for Perf and 0 for Eff (Slow)
                        //  - we need 0 = Perf so ti will sort first
                        //  - also cap to 2 bits

                        auto eclass = max_efficiency - info->Processor.EfficiencyClass;
                        if (eclass < 4) {
                            processor.eclass = eclass;
                        } else {
                            processor.eclass = 3;
                        }

                        auto bits = CountSetBits (processor.affinity);
                        while (bits--) {
                            processor.affinity = info->Processor.GroupMask [0].Mask;
                            processor.number = numbers [processor.group]++;

                            // extend 'processor' affinity to MODULE or L2 CACHE, whichever spans more

                            auto q = buffer.data ();
                            while (true) {
                                auto extend = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *) q;
                                if (extend->Size) {
                                    switch (extend->Relationship) {
                                        case RelationProcessorModule:
                                            if (extend->Processor.GroupCount == 0) {
                                                extend->Processor.GroupCount = 1;
                                            }
                                            for (auto i = 0u; i != extend->Processor.GroupCount; ++i) {
                                                if (processor.group == extend->Processor.GroupMask [i].Group) {
                                                    if (processor.affinity & extend->Processor.GroupMask [i].Mask) {
                                                        processor.affinity |= extend->Processor.GroupMask [i].Mask;
                                                    }
                                                }
                                            }
                                            break;
                                        case RelationCache:
                                            if (extend->Cache.Level == 2) {
                                                if (extend->Cache.GroupCount == 0) {
                                                    extend->Cache.GroupCount = 1;
                                                }
                                                for (auto i = 0u; i != extend->Cache.GroupCount; ++i) {
                                                    if (processor.group == extend->Cache.GroupMasks [i].Group) {
                                                        if (processor.affinity & extend->Cache.GroupMasks [i].Mask) {
                                                            processor.affinity |= extend->Cache.GroupMasks [i].Mask;
                                                        }
                                                    }
                                                }
                                            }
                                            break;
                                    }

                                    q += extend->Size;
                                } else
                                    break;
                            }

                            // insert

                            processors.push_back (processor);
                            processor.smt++;
                        }
                    }
                    p += info->Size;
                } else
                    break;
            }
        } else {
            raddi::log::error (12, "GetLogicalProcessorInformationEx");
        }
    }
    
    if (processors.empty ()) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer [MAXIMUM_PROC_PER_GROUP];
        DWORD n = sizeof buffer;

        if (GetLogicalProcessorInformation (buffer, &n)) {
            n /= sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

            // evaluate SMT level
            //  0 - no SMT
            //  2 - 2 threads per core, 4 for 4-way SMT, etc.

            auto smt = 0u;
            for (auto i = 0u; i != n; ++i) {
                if (buffer [i].Relationship == RelationProcessorCore) {
                    if (buffer [i].ProcessorCore.Flags & 1) {
                        smt = CountSetBits (buffer [i].ProcessorMask);
                        break;
                    }
                }
            }

            // do L2 caches span multiple cores
            //  - then we will expand threads' affinity to whole L2

            bool l2 = false;
            for (auto i = 0u; i != n; ++i) {
                if (buffer [i].Relationship == RelationCache) {
                    if (buffer [i].Cache.Level == 2) {
                        if (CountSetBits (buffer [i].ProcessorMask) > smt) {
                            l2 = true;
                            break;
                        }
                    }
                }
            }

            // create logical processor entries
            //  - default affinity is per-core

            Processor processor;
            for (auto i = 0u; i != n; ++i) {
                if (buffer [i].Relationship == RelationProcessorCore) {

                    processor.affinity = buffer [i].ProcessorMask;
                    processor.smt = 0;

                    auto bits = CountSetBits (buffer [i].ProcessorMask);
                    while (bits--) {
                        processors.push_back (processor);
                        processor.number++;
                        processor.smt++;
                    }
                }
            }

            // if desirable, expand affinity to whole L2 tile/module

            if (l2) {
                for (auto i = 0u; i != n; ++i) {
                    if (buffer [i].Relationship == RelationCache) {
                        if (buffer [i].Cache.Level == 2) {
                            
                            for (auto & processor : processors) {
                                if (processor.affinity & buffer [i].ProcessorMask) {
                                    processor.affinity |= buffer [i].ProcessorMask;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            raddi::log::error (12, "GetLogicalProcessorInformation");
        }
    }

    std::sort (processors.begin (), processors.end ());
    return processors;
}

const VS_FIXEDFILEINFO * GetModuleVersionInfo (HMODULE handle) {
    if (HRSRC hRsrc = FindResource (handle, MAKEINTRESOURCE (1), RT_VERSION))
        if (HGLOBAL hGlobal = LoadResource (handle, hRsrc)) {
            auto p = reinterpret_cast <const DWORD *> (LockResource (hGlobal));
            auto e = p + (SizeofResource (handle, hRsrc) - sizeof (VS_FIXEDFILEINFO)) / sizeof *p;

            p = std::find (p, e, 0xFEEF04BDu);
            if (p != e)
                return reinterpret_cast <const VS_FIXEDFILEINFO *> (p);
        }

    return nullptr;
}

const VS_FIXEDFILEINFO * GetCurrentProcessVersionInfo () {
    return GetModuleVersionInfo (NULL);
}

bool IsPathAbsolute (std::wstring_view path) {
    return ((path.length () >= 4) && std::iswalpha (path [0]) && path [1] == L':' && path [2] == L'\\')
        || ((path.length () >= 5) && path [0] == L'\\' && path [1] == L'\\')
        ;
}

bool GetProcessSessionId (DWORD pid, DWORD * id) {
#if defined (_M_ARM64)
    return ProcessIdToSessionId (pid, id);
#else
    if (ptrProcessIdToSessionId) {
        if (ptrProcessIdToSessionId (pid, id))
            return true;
    }
#endif
    return false;
}

DWORD GetCurrentProcessSessionId () {
    DWORD id;
    if (GetProcessSessionId (GetCurrentProcessId (), &id))
        return id;

#if defined (_M_AMD64)
    return (DWORD) *reinterpret_cast <std::uint64_t *> (__readgsqword (0x60) + 0x02C0);
#elif defined (_M_IX86)
    __asm mov eax, fs:[0x18];
    __asm mov eax, [eax + 0x30];
    __asm mov eax, [eax + 0x1d4];
#else
    return 0;
#endif
}
