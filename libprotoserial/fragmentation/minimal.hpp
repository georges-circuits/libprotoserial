/*
 * This file is a part of the libprotoserial project
 * https://github.com/georges-circuits/libprotoserial
 * 
 * Copyright (C) 2022 Jiří Maňák - All Rights Reserved
 * For contact information visit https://manakjiri.eu/
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/gpl.html>
 */


#ifndef _SP_FRAGMENTATION_MINIMAL
#define _SP_FRAGMENTATION_MINIMAL

#include "libprotoserial/fragmentation/fragmentation.hpp"

namespace sp
{
    template<typename Header>
    class base_minimal_handler : public fragmentation_handler
    {
        //using Header = headers::fragment_8b8b;
        using message_types = typename Header::message_types;

        enum tr_states
        {
            NEW,
            SENT,
            NEXT,
            WAITING,
            RETRY
        };
        struct tr_wrapper : public transfer_handler<Header>
        {
            using transfer_handler<Header>::transfer_handler;
            
            clock::time_point sent_at = never();
            index_type current_fragment = 0;
            tr_states state = tr_states::NEW;
        };

        struct peer_state
        {
            peer_state(address_type a, const configuration & c)  :
                addr(a), tx_rate(c.peer_rate), last_rx(never()), tx_holdoff(clock::now()) {}

            address_type addr;
            /* from our point of view */
            uint tx_rate;
            /* from our point of view */
            clock::time_point last_rx, tx_holdoff;

            bool in_transmit_holdoff() const {return tx_holdoff > clock::now();}
        };

        auto peer_find(address_type addr)
        {
            auto peer = std::find_if(_peer_states.begin(), _peer_states.end(), [&](const peer_state & ps){
                return ps.addr == addr;
            });
            if (peer == _peer_states.end())
            {
                _peer_states.emplace_front(addr, _config);
                return _peer_states.begin();
            }
            else
                return peer;
        }

        Header make_header(types type, index_type fragment_pos, const transfer & t)
        {
            return Header(type, fragment_pos, t.fragments_count(), t.get_id(), t.get_prev_id(), 0);
        }
        Header make_header(types type, const Header & h)
        {
            return Header(type, h.fragment(), h.fragments_total(), h.get_id(), h.get_prev_id(), 0);
        }

        /* data size before the header is added */
        size_type max_fragment_data_size() const
        {
            /* _interface->max_data_size() is the maximum size of a fragment's data */
            return _interface->max_data_size() - sizeof(Header)
        }

        
        
        inline auto find_outgoing(std::function<bool(const tr_wrapper &)> pred)
        {
            return std::find_if(_outgoing_transfers.begin(), _outgoing_transfers.end(), pred);
        }

        inline auto find_incoming(std::function<bool(const tr_wrapper &)> pred)
        {
            return std::find_if(_incoming_transfers.begin(), _incoming_transfers.end(), pred);
        }



        void transmit_began_callback(object_id_type id)
        {
            auto pt = find_outgoing([id](const tr_wrapper & tr){
                tr.object_id() == id;
            });
            if (pt != _outgoing_transfers.end())
            {
                
            }
        }

        public:

        void transmit(transfer t)
        {
#ifdef SP_FRAGMENTATION_DEBUG
            std::cout << "transmit got: " << t << std::endl;
#elif defined(SP_FRAGMENTATION_WARNING)
            std::cout << "transmit got id " << (int)t.get_id() << std::endl;
#endif
            _outgoing_transfers.emplace_back(std::move(t), max_fragment_data_size());
        }

        void print_debug() const
        {
#ifndef SP_NO_IOSTREAM
            std::cout << "incoming_transfers: " << _incoming_transfers.size() << std::endl;
            for (const auto & t : _incoming_transfers)
                std::cout << static_cast<transfer>(t) << std::endl;
            
            std::cout << "outgoing_transfers: " << _outgoing_transfers.size() << std::endl;
            for (const auto & t : _outgoing_transfers)
                std::cout << static_cast<transfer>(t) << std::endl;
#endif
        }

        private:

        std::list<peer_state> _peer_states;
        std::list<tr_wrapper> _incoming_transfers, _outgoing_transfers;
    };

    template<typename Header>
    class minimal_handler : public base_minimal_handler<Header>
    {


    };
}

#endif
