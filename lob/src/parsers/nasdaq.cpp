#include <cmath>
#include <cstdint>
#include <span>
#include "feed.h"
#include "itch.h"

namespace parsers::nasdaq {
    inline constexpr uint16_t kLenR = 39,
           kLenA = 36, kLenF = 40,
           kLenX = 23, kLenD = 19,
           kLenU = 35, kLenP = 44,
           kLenE = 31, kLenC = 36;

    inline constexpr size_t kOffLocate = 1,
           kOffRef = 11, kOffSide = 19,
           kOffShares = 20, kOffPrice = 32;

    /*
     * base header parsing is deferred for now
     * */
    bool parse_R(
            std::span<const uint8_t> data,
            StockDir& dir
            ) {
        if (data.size() != kLenR) {
            return false;
        }

        dir.locate = be16(&data[kOffLocate]);
        // dir.symbol = trim(data[11..18]);
        auto sym = data.subspan(11, 8);  // a view, no copy

        // materialize to string...
        // then strip trailing 0x20:
        dir.symbol = std::string(sym.begin(), sym.end());
        dir.symbol.erase(dir.symbol.find_last_not_of(" ") + 1);

        return true;
    }

    void build_order(std::span<const uint8_t> data, AddOrder& add) {
        uint16_t locate = be16(&data[kOffLocate]);
        uint64_t ref = 0;
        uint32_t shares = 0;
        uint64_t prices = 0;

        for(int i = 0; i < 8; i++) {
            ref = (ref << 8) | data[11+i];
        }
        char side = char(data[19]);

        for(int i = 0; i < 4; i++) {
            shares = (shares << 8) | data[20+i];
        }

        for (int i = 0; i < 4; i++) {
            prices = (prices << 8) | data[32+i];
        }

        add.locate = locate;
        add.ref = ref;
        add.shares = shares;
        add.price_ticks = prices;
        add.side = side;
        add.mpid = 0;
    }

    bool parse_A(
            std::span<const uint8_t> data,
            AddOrder& add
            ) {

        if (data.size() != kLenA) {
            return false;
        }

        // for(int i = 0, j = 8; i < 8; i++){
        //     int idx = 11+i;
        //     int shifter = (j * (8-i-1));
        //
        //     ref = ref | (static_cast<uint64_t>(data[idx]) << shifter);
        // }
        // for(int i = 0, j = 8; i < 4; i++){
        //     int idx = 20+i;
        //     int shifter = (j * (4-i-1));
        //
        //     shares =shares | (static_cast<uint32_t>(data[idx]) << shifter);
        // }
        // for(int i = 0, j = 8; i < 4; i++){
        //     int idx = 32+i;
        //     int shifter = (j * (4-i-1));
        //
        //     prices = prices | (static_cast<uint64_t>(data[idx]) << shifter);
        // }

        build_order(data, add);

        return true;
    }

    bool parse_F(
            std::span<const uint8_t> data,
            AddOrder& add
            ) {

        if(data.size() != kLenF) {
            return false;
        }

        build_order(data, add);

        uint32_t mpid = 0;
        for(int i = 0; i < 4; i++) {
            mpid = (mpid << 8) | data[36+i];
        }
        add.mpid = mpid;

        return true;
    }

    bool parse_X(
            std::span<const uint8_t> data,
            Cancel& cancel
            ) {

        if (data.size() != kLenX) {
            return false;
        }

        uint64_t ref = 0;

        for(int i = 0; i < 8; i++){
            ref = (ref << 8) | data[11+i];
        }

        cancel.ref = ref;

        for(int i = 0; i < 4; i++) {
            cancel.shares = (cancel.shares << 8) | data[19+i];
        }

        return true;
    }

    bool parse_D(
            std::span<const uint8_t> data,
            Delete& del
            ) {

        if(data.size() != kLenD) {
            return false;
        }

        for(int i = 0; i < 8; i++) {
            del.ref = (del.ref << 8) | data[11+i];
        }

        return true;
    }

    bool parse_E(
            std::span<const uint8_t> data,
            Exec& exec
            ) {

        if(data.size() != kLenE) {
            return false;
        }

        for(int i = 0; i < 8; i++) {
            exec.ref = (exec.ref << 8) | data[11+i];
        }
        for(int i = 0; i < 4; i++) {
            exec.shares = (exec.shares << 8) | data[19+i];
        }
        for(int i = 0; i < 8; i++) {
            exec.match = (exec.match << 8) | data[23+i];
        }

        return true;
    }

    bool parse_C(
            std::span<const uint8_t> data,
            ExecPx& execp
            ) {

        if (data.size() != kLenC) {
            return false;
        }

        for(int i = 0; i < 8; i++) {
            execp.ref = (execp.ref << 8) | data[11+i];
        }
        for(int i = 0; i < 4; i++) {
            execp.shares = (execp.shares << 8) | data[19+i];
        }
         for(int i = 0; i < 8; i++) {
            execp.match = (execp.match << 8) | data[23+i];
        }
        execp.printable = char(data[31]);  // byte 31: 'Y'/'N' (NOT price — see 6-sample check)
        for(int i = 0; i < 4; i++) {
            execp.price = (execp.price << 8) | data[32+i];  // price lives at 32..35, after printable
        }

        // data[35] is the last price byte (32..35), already consumed above

        return true;
    }

    bool parse_U(
            std::span<const uint8_t> data,
            Replace& r
            ) {

        if(data.size() != kLenU) {
            return false;
        }

        for(int i = 0; i < 8; i++) {
            r.old_ref = (r.old_ref << 8) | data[11+i];
        }
        for(int i = 0; i < 8; i++) {
            r.new_ref = (r.new_ref << 8) | data[19+i];
        }
        for(int i = 0; i < 4; i++) {
            r.shares = (r.shares << 8) | data[27+i];
        }
        for(int i = 0; i < 4; i++) {
            r.price = (r.price << 8) | data[31+i];
        }

        return true;
    }
}

/*
 * 
- D (19B): ref from 11..18, nothing else. Fill Delete{ref}. One field.
- E (31B): ref 11..18, executed shares 19..22. Match number at 23..30 read-and-drop (audit trail, not book state). Fill Exec{ref, shares}.
- C (36B): E plus price at 31..34 and printable flag at 35. Fill ExecPx{ref, shares, price}; keep the flag only if you tape-tag hidden prints later.
- U (35B): old ref 11..18, new ref 19..26, shares 27..30, price 31..34. Fill Replace{old_ref, new_ref, shares, price} — the only two-id letter, and the reason AddOrder alone couldn't cover the set.
 * */
