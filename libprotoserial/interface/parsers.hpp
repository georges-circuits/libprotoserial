
#ifndef _SP_INTERFACE_PARSERS
#define _SP_INTERFACE_PARSERS

#include "libprotoserial/interface/interface.hpp"

#include <stdexcept>

namespace sp
{
    namespace parsers
    {
        struct bad_checksum : std::exception {
            const char * what () const throw () {return "bad_checksum";}
        };

        struct bad_size : std::exception {
            const char * what () const throw () {return "bad_size";}
        };

        template<typename header, typename footer>
        interface::packet parse_packet(bytes && buff, const interface * i)
        {
            bytes b = buff;
            //std::cout << "parse_packet got: " << buff << std::endl;
            /* copy the header into the header struct */
            header h;
            std::copy(b.begin(), b.begin() + sizeof(h), reinterpret_cast<byte*>(&h));
            if (!h.is_size_valid()) throw bad_size();
            /* copy the footer, shrink the container by the footer size and compute the checksum */
            footer f_parsed;
            std::copy(b.end() - sizeof(footer), b.end(), reinterpret_cast<byte*>(&f_parsed));
            b.shrink(0, sizeof(footer));
            footer f_computed(b);
            if (f_parsed.hash != f_computed.hash) throw bad_checksum();
            /* shrink the container by the header and return the packet object */
            b.shrink(sizeof(h), 0);
            return interface::packet(interface::address_type(h.source), interface::address_type(h.destination), 
                std::move(b), i);
        }
    }
}

#endif
