#ifndef RADDI_REQUEST_TCC
#define RADDI_REQUEST_TCC

template <std::size_t restriction>
    typename std::map <std::pair <std::uint32_t, std::uint32_t>, std::uint32_t>
raddi::request::short_history <restriction>::decode (std::size_t size) const {

    auto t = this->threshold;
    auto i = this->length (size);
    std::map <std::pair <std::uint32_t, std::uint32_t>, std::uint32_t> results;

    while (i--) {
        auto st = (this->span [i].threshold [0] << 0)
                | (this->span [i].threshold [1] << 8)
                | (this->span [i].threshold [2] << 16);
        auto sn = (this->span [i].number [0] << 0)
                | (this->span [i].number [1] << 8)
                | (this->span [i].number [2] << 16);
        ++st;

        results [std::make_pair (t - st, t - 1)] = (sn < 0x00FFFFFF) ? (sn + 1) : 0xFFFFFFFF;
        t -= st;
    }
    return results;
}

#endif
