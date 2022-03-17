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

#ifndef _SP_INTERFACE_BUFFERED
#define _SP_INTERFACE_BUFFERED

#include "libprotoserial/interface/interface.hpp"

namespace sp
{
    namespace detail
    {

        class buffered_interface : public interface
        {
            public:
            
        	/* - name should uniquely identify the interface on this device
			 * - address is the interface address, when a fragment is received where destination() == address
			 *   then the receive_event is emitted, otherwise the other_receive_event is emitted
			 * - max_queue_size sets the maximum number of fragments the transmit queue can hold
			 * - buffer_size sets the size of the receive buffer in bytes
			 */
            buffered_interface(std::string name, address_type address, uint max_queue_size, uint buffer_size):
                interface(name, address, max_queue_size), _rx_buffer(buffer_size) {}


            /* iterator pointing into the _rx_buffer, it supports wrapping */
            struct circular_iterator 
            {
                using iterator_category = std::forward_iterator_tag;
                using difference_type   = bytes::difference_type;
                using value_type        = bytes::value_type;
                using pointer           = bytes::pointer; 
                using reference         = bytes::reference;

                circular_iterator(bytes::iterator begin, bytes::iterator end, bytes::iterator start) : 
                    _begin(begin), _end(end), _current(start) {}

                circular_iterator(bytes & buff, bytes::iterator start) :
                    circular_iterator(buff.begin(), buff.end(), start) {}

                circular_iterator():
                    circular_iterator(nullptr, nullptr, nullptr) {}

                circular_iterator(const circular_iterator &) = default;
                circular_iterator(circular_iterator &&) = default;
                circular_iterator & operator=(const circular_iterator &) = default;
                circular_iterator & operator=(circular_iterator &&) = default;

                reference operator*() const { return *_current; }
                pointer operator->() { return _current; }

                // Prefix increment
                circular_iterator& operator++() 
                {
                    ++_current; 
                    if (_current >= _end) _current = _begin;
                    return *this;
                }  

                circular_iterator& operator+=(uint shift)
                {
                    _current += shift;
                    if (_current >= _end) _current -= (_end - _begin);
                    return *this; 
                }

                friend circular_iterator operator+(circular_iterator lhs, uint rhs)
                {
                    lhs += rhs;
                    return lhs; 
                }

                /* both iterators need to point within the same buffer and assuming you know which is leading and which lagging,
                this will return an integer which will be the distance between the two iterators */
                friend difference_type distance(const circular_iterator & lagging, const circular_iterator & leading)
                {
                    difference_type diff = leading._current - lagging._current;
                    return diff >= 0 ? diff : diff + (lagging._end - lagging._begin);
                }

                friend bool operator== (const circular_iterator& a, const circular_iterator& b) { return a._current == b._current; };
                friend bool operator!= (const circular_iterator& a, const circular_iterator& b) { return a._current != b._current; };

                /* _begin is the first byte of the container, _end is one past the last byte of the container, 
                _current is in the interval [_begin, _end) */
                bytes::iterator _begin, _end, _current;
                private:
            }; 

            /* returns the interator that points to the beginning of the _rx_buffer, store this in
            member variable at init and use it to access the buffer */
            circular_iterator get_rx_buffer() {return circular_iterator(_rx_buffer, _rx_buffer.begin());}

            private:

            bytes _rx_buffer;
        };
    }
} // namespace sp


#endif

