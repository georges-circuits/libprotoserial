
#include <libprotoserial/container.hpp>

#include <iostream>
#include <iomanip>
#include "gtest/gtest.h"

using namespace std;


ostream& operator<< (ostream& os, const sp::bytes& obj) 
{
    os << "[ ";
    for (uint i = 0; i < obj.size(); i++)
        os << (int)obj[i] << ' ';
    return os << ']';
}


/* TEST(Container, Constructor) 
{
    sp::bytes b1(10);

    EXPECT_TRUE(b1.data() != nullptr);
    EXPECT_EQ(10, b1.size());


    sp::bytes b2(2, 10, 1);

    EXPECT_TRUE(b2.data() != nullptr);
    EXPECT_EQ(10, b2.size());
    EXPECT_TRUE(13 >= b2.get_capacity());
}

TEST(Container, Access)
{
    sp::bytes b1(10);

    for (uint i = 0; i < b1.size(); i++)
        b1[i] = (sp::byte)(i + 10);
    
     
}*/


int main(int argc, char const *argv[])
{
    //sp::bytes b2 = {1, 2, 3, 4, 5};

    sp::bytes b1 (2, 5, 1);
    b1.set((byte)1);

    b1[0] = (byte)10;
    b1[1] = (byte)((int)b1[0] + 1);

    cout << b1 << endl;

    b1.expand(1, 1);

    cout << b1 << endl;


    sp::bytes b2 ("12345");
    sp::bytes b3 = b2;

    cout << b3 << ' ' << b2 << endl;

    cout << (b2.data() == b3.data()) << ' ' << (b2 == b3) << endl;

    b3 = move(b2);
    
    cout << b3 << ' ' << b2 << endl;

    b2 = move(b3);

    cout << b3 << ' ' << b2 << endl;


    return 0; 
}



