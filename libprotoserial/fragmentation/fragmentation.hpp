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

/* 
 * > We can build the fragment fragmentation logic on top of the interface, 
 * > this logic should have its own internal buffer for fragments received
 * > from events, because once the event firess, the fragment is forgotten on 
 * > the interface side to avoid the need for direct access to the interface's 
 * > RX queue.
 */


#ifndef _SP_FRAGMENTATION_HANDLER
#define _SP_FRAGMENTATION_HANDLER

#include "libprotoserial/interface/parsers.hpp"
#include "libprotoserial/interface.hpp"

#include "libprotoserial/clock.hpp"

#include "libprotoserial/fragmentation/headers.hpp"
#include "libprotoserial/fragmentation/transfer.hpp"

#include <memory>

#ifndef SP_NO_IOSTREAM
#include <iostream>
/* it is hard to debug someting that happens every 100 transfers using 
 * debugger alone, these enable different levels of debug prints */
//#define SP_FRAGMENTATION_DEBUG
//#define SP_FRAGMENTATION_WARNING
#endif


#ifdef SP_FRAGMENTATION_DEBUG
#define SP_FRAGMENTATION_WARNING
#endif

namespace sp
{
    class fragmentation_handler
    {
        public:

        using Header = headers::fragment_8b16b;
        using types = Header::message_types;
        using index_type = transfer::index_type;
        using id_type = transfer::id_type;
        using size_type = fragment::data_type::size_type;

        private:

        struct transfer_wrapper : public transfer
        {
            transfer_wrapper(transfer && t, fragmentation_handler * handler) :
                transfer(std::move(t)), _handler(handler) {}

            /* returns the number of fragments needed to transmit this transfer,
            this obviously depends on the mode but we can make some assumptions:
            -   when in mode1 transfer has internally preallocated the needed number of 
                slots for fragments so this returns that number
            -   when in mode 2 slots are allocated on demand with new data, so this 
                returns the computed number of needed fragments based on the data size */
            index_type _fragments_count() const 
            {
                if (_is_complete())
                    /* mode 2 */
                    return data_size() / _handler->max_fragment_size() + 
                        (data_size() % _handler->max_fragment_size() == 0 ? 0 : 1);
                else
                    /* mode 1 */
                    return _data.size();
            }
            
            /* mode 1) only */
            bool _is_complete() const
            {
                for (auto b = _begin(); b != _end(); ++b)
                {
                    if (b->is_empty())
                        return false;
                }
                return true;
            }

            /* mode 1) only */
            index_type _missing_fragment() const
            {
                for (std::size_t i = 0; i < _data.size(); ++i)
                {
                    if (_data.at(i).is_empty())
                        return (index_type)(i + 1);
                }
                return 0; //_fragments_count();
            }

            /* mode 2) only, preferably */
            fragment _get_fragment(index_type fragment_pos) const
            {
                if (fragment_pos == 0) throw std::invalid_argument("fragment_pos == 0");
                fragment_pos -= 1;
                auto p_size = _handler->max_fragment_size();
                if (fragment_pos * p_size > data_size()) throw std::invalid_argument("fragment_pos * p_size > data_size()");

                bytes data(sizeof(Header), 0, p_size);
                auto b = data_begin() + fragment_pos * p_size, e = data_end();
                for (; b != e && data.size() < p_size; ++b) data.push_back(*b);
                
                return fragment(destination(), std::move(data));
            }

            fragmentation_handler * _handler;
        };
        
        /* this wraps the underlaying transfer to strip it off otherwise unneeded values
        like timeouts and various housekeeping stuff */
        struct transfer_progress
        {
            /* using a pointer here to free up space when we get rid of the transfer once it's done 
            but need to keep the transfer_progress object for a while longer */
            std::unique_ptr<transfer_wrapper> tr;
            clock::time_point timestamp_accessed;
            uint retransmitions = 0;
            /* this should always match the tr->get_id() */
            id_type id;

            transfer_progress(transfer_wrapper && t) :
                tr(std::unique_ptr<transfer_wrapper>(new transfer_wrapper(std::move(t)))), 
                    timestamp_accessed(clock::now()), id(tr->get_id()) {}

            void transmit_done() {timestamp_accessed = clock::now();}
            void retransmit_done() {timestamp_accessed = clock::now(); ++retransmitions;}
        };


        public:

        fragmentation_handler(interface_identifier iid, size_type max_fragment_size, clock::duration retransmit_time, 
            clock::duration drop_time, uint retransmit_multiplier) :
                _retransmit_time(retransmit_time), _drop_time(drop_time), _interface_identifier(iid),
                _max_fragment_size(max_fragment_size - sizeof(Header)), _retransmit_multiplier(retransmit_multiplier) {}


        /* the callback handles the incoming fragments, it does not handle any timeouts, sending requests, 
        or anything that assumes periodicity, the main_task is for that */
        void receive_callback(fragment p) noexcept
        {
#ifdef SP_FRAGMENTATION_DEBUG
            std::cout << "receive_callback got: " << p << std::endl;
#endif
            if (p && p.data().size() >= sizeof(Header))
            {
                /* copy the header from the fragment data after some obvious sanity checks, discard 
                the header from fragment's data afterward */
                auto h = parsers::byte_copy<Header>(p.data().begin());
                if (h.is_valid())
                {
                    p.data().shrink(sizeof(Header), 0);
                    handle_fragment(h, std::move(p));
                }
            }
        }

        void main_task()
        {
            auto it = _incoming_transfers.begin();
            while (it != _incoming_transfers.end())
            {
                if (!it->tr)
                {
                    /* we have the transfer_progress object but it does not own any transfer, which means
                    that the block below must have been executed in the near past
                    drop the transfer_progress after some sufficiently long period since its last access */
                    //TODO get rid of the 5 and make it a config thing somehow
                    if (older_than(it->timestamp_accessed, _drop_time * 5))
                        it = _incoming_transfers.erase(it);
                }
                else if (it->tr->_is_complete() && can_transmit())
                {
                    /* checks whether any of the transfers is complete, if so emit it as receive event */
                    transmit_event.emit(std::move(
                        fragment(it->tr->source(), 
                        to_bytes(make_header(types::FRAGMENT_ACK, it->tr->_fragments_count(), *it)))
                    ));
                    transfer_receive_event.emit(std::move(*it->tr));
                    it->tr.reset();
                    /* std::move moved the local copy of the transfer out of the handler, but we
                    will hold onto that transfer_progress structure for a while longer. in case
                    the ACK fragment gets lost, the source will try to retransmit this transfer
                    thinking that we didn't notice it */
                }
                else
                {
                    if (older_than(it->tr->timestamp_modified(), _drop_time))
                    {
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "timed out incoming: " << it->tr << std::endl;
#endif
                        /* drop the incoming transfer because it is inactive for too long */
                        it = _incoming_transfers.erase(it);
                    }
                    else if (can_transmit() && older_than(it->tr->timestamp_modified(), _retransmit_time) && 
                        older_than(it->timestamp_accessed, _retransmit_time))
                    {
                        /* find the missing fragment's index and request a retransmit */
                        auto index = it->tr->_missing_fragment();
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "requesting retransmit for id " << it->id << " index " << index << std::endl;
#endif
                        transmit_event.emit(std::move(
                            fragment(it->tr->source(), 
                            to_bytes(make_header(types::FRAGMENT_REQ, index, *it)))
                        ));
                        it->retransmit_done();
                    }
                }
                /* check again because an erase can happen in this branch as well, 
                we don't need to worry about skipping a transfer when the erase happens
                since we will return into this function later anyway */
                if (it != _incoming_transfers.end())
                    ++it;
            }
            /* check for stale outgoing transfers, it may happen that the ACK didn't arrive, 
            it is not ACKed back, so that can happen */
            it = _outgoing_transfers.begin();
            while (it != _outgoing_transfers.end())
            {
                if (older_than(it->timestamp_accessed, _drop_time))
                {
                    /* drop the outgoing transfer because it is inactive for too long */
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "timed out outgoing id " << it->id << std::endl;
#endif
                    it = _outgoing_transfers.erase(it);
                }
                else if (can_transmit() && it->retransmitions < it->tr->_fragments_count() * _retransmit_multiplier &&
                    older_than(it->timestamp_accessed, _retransmit_time))
                {
                    /* either the destination is dead or the first fragment got lost during
                    transit, try to retransmit the first fragment */
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "retransmitting first fragment of id " << it->id << std::endl;
#endif
                    transmit_event.emit(std::move(serialize_fragment(types::FRAGMENT, 1, *it)));
                    it->retransmit_done();
                }
                else
                    ++it;
            }
        }

        void transmit(transfer t)
        {
#ifdef SP_FRAGMENTATION_DEBUG
            std::cout << "transmit got: " << t << std::endl;
#elif defined(SP_FRAGMENTATION_WARNING)
            std::cout << "transmit got id " << t.get_id() << std::endl;
#endif
            /* transmit all fragments within this transfer and store it in case we get a retransmit request */
            auto & tp = _outgoing_transfers.emplace_back(transfer_wrapper(std::move(t), this));
            for (index_type fragment_pos = 1; fragment_pos <= tp.tr->_fragments_count(); ++fragment_pos)
            {
#ifdef SP_FRAGMENTATION_DEBUG
                std::cout << "transmit emitting event" << std::endl;
#endif
                if (can_transmit())
                    transmit_event.emit(std::move(serialize_fragment(types::FRAGMENT, fragment_pos, tp)));
                else
                    break;
            }
            tp.transmit_done();
        }

        size_type max_fragment_size() const
        {
            return _max_fragment_size;
        }

        void print_debug() const
        {
#ifndef SP_NO_IOSTREAM
            std::cout << "incoming_transfers: " << _incoming_transfers.size() << std::endl;
            for (const auto & t : _incoming_transfers)
                std::cout << t.tr << std::endl;
            
            std::cout << "outgoing_transfers: " << _outgoing_transfers.size() << std::endl;
            for (const auto & t : _outgoing_transfers)
                std::cout << t.tr << std::endl;
#endif
        }

        /* shortcut for event subscribe */
        void bind_to(interface & l)
        {
            l.receive_event.subscribe(&fragmentation_handler::receive_callback, this);
            l.status_event.subscribe(&fragmentation_handler::interface_status_callback, this);
            transmit_event.subscribe(&interface::write_noexcept, &l);
        }

        /* fires when the handler wants to transmit a fragment, complemented by receive_callback */
        subject<fragment> transmit_event;
        /* fires when the handler receives and fully reconstructs a fragment, complemented by transmit */
        subject<transfer> transfer_receive_event;
        /* fires when ACK was received from destination for this transfer */
        subject<transfer_metadata> transfer_ack_event;

        private:

        void interface_status_callback(interface::status status)
        {
            _interface_status = status;
        }

        Header make_header(types type, index_type fragment_pos, const transfer_progress & tp)
        {
            return Header(type, fragment_pos, tp.tr->_fragments_count(), tp.tr->get_id(), tp.tr->get_prev_id());
        }

        /* copy the data of the fragment within the transfer and create an fragment from it */
        fragment serialize_fragment(types type, index_type fragment_pos, const transfer_progress & tp)
        {
            auto p = tp.tr->_get_fragment(fragment_pos);
            bytes h = to_bytes(make_header(type, fragment_pos, tp));
            p.data().push_front(std::move(h));
            return p;
        }

        void handle_fragment(const Header & h, fragment && p)
        {
#ifdef SP_FRAGMENTATION_DEBUG
            std::cout << "handle_fragment got: " << p << std::endl;
#endif
            /* handle the reception of user fragments and their ordering */
            if (h.type() == types::FRAGMENT)
            {
                /* branch for handling _incoming_transfers */
                /* check if we already know that incoming transfer ID */
                auto it = std::find_if(_incoming_transfers.begin(), _incoming_transfers.end(), 
                    [&](const auto & t){
                        //FIXME use tr->match() in both cases, as per spec
                        if (t.tr)
                            return t.tr->get_id() == h.get_id() && t.tr->match(p);
                        else
                            return t.id == h.get_id();
                });

                if (it == _incoming_transfers.end())
                {
#ifdef SP_FRAGMENTATION_DEBUG
                    std::cout << "creating new incoming transfer id " << h.get_id() << std::endl;
#endif
                    /* we don't know this transfer ID, create new incoming transfer */
                    auto& t = _incoming_transfers.emplace_back(
                        transfer_progress(transfer_wrapper(transfer(_interface_identifier, h), this))
                    );
                    t.tr->_assign(h.fragment(), std::move(p));
                }
                else
                {
                    /* we know this ID, now we need to check if we have already received this transfer and
                    is therefor duplicate, or whether we are in the process of receiving it */
                    if (it->tr)
                    {
#ifdef SP_FRAGMENTATION_DEBUG
                        std::cout << "assigning to existing incoming transfer id " << h.get_id() << " at " << (int)h.fragment() << " of " << (int)h.fragments_total() << std::endl;
#endif
                        /* the ID is in known transfers, we need to add the incoming fragment to it */
                        it->tr->_assign(h.fragment(), std::move(p));
                    }
                    else
                    {
                        /* we, for some reason, received a fragment of already received transfer, the ACK
                        from us probably got lost in transit, just reply with another ACK and ignore this fragment */
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "sending ACK for already received id " << h.get_id() << std::endl;
#endif
                        if (can_transmit())
                        {
                            transmit_event.emit(std::move(fragment(p.source(), 
                                std::move(to_bytes(Header(types::FRAGMENT_ACK, h.fragment(), h.fragments_total(), h.get_id(), h.get_prev_id()))))
                            ));
                        }
                    }
                }
            }
            else
            {
                /* branch for handling _outgoing_transfers */
                auto it = std::find_if(_outgoing_transfers.begin(), _outgoing_transfers.end(), 
                    [&](const auto & t){return t.tr->get_id() == h.get_id() && t.tr->match_as_response(p);});
                
                if (it != _outgoing_transfers.end())
                {
                    if (h.type() == types::FRAGMENT_REQ && can_transmit())
                    {
#ifdef SP_FRAGMENTATION_WARNING
                        std::cout << "handling retransmit request of id " << h.get_id() << " fragment " << (int)h.fragment() << " of " << (int)h.fragments_total() << std::endl;
#endif
                        /* fullfill the retransmit request */
                        transmit_event.emit(std::move(serialize_fragment(types::FRAGMENT, h.fragment(), *it)));
                        it->retransmit_done();
                    }
                    else if (h.type() == types::FRAGMENT_ACK)
                    {
#ifdef SP_FRAGMENTATION_DEBUG
                        std::cout << "got fragment ACK for id " << h.get_id() << std::endl;
#endif
                        /* emit the ACK event for the sender and destroy this outgoing transfer
                        since we've done our job and don't need it anymore - in contrast to the 
                        incoming transfer where the transmitted ACK may not be received, here we
                        can be sure */
                        transfer_ack_event.emit(it->tr->get_metadata());
                        it = _outgoing_transfers.erase(it);
                    }
                }
            }
        }

        bool can_transmit() const {return _interface_status.available_transmit_slots != 0;}

        std::list<transfer_progress> _incoming_transfers, _outgoing_transfers;
        clock::duration _retransmit_time, _drop_time;
        interface_identifier _interface_identifier;
        interface::status _interface_status;
        size_type _max_fragment_size;
        uint _retransmit_multiplier;
    };
}




#endif
