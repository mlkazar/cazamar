#include <memory>
#include <iostream>
 
struct Good: std::enable_shared_from_this<Good> // note: public inheritance
{
    long x;
    std::shared_ptr<Good> getptr() {
        return shared_from_this();
    }
    Good() { x=0x1234556;}
       
};
 
struct Bad
{
    std::shared_ptr<Bad> getptr() {
        return std::shared_ptr<Bad>(this);
    }
    ~Bad() { std::cout << "Bad::~Bad() called\n"; }
};
 
class Frob {
    int a;

    class Bozo {
        void func() {
            printf("a is %d\n", a);
        }

    public:
        Bozo() {
            func();
        }
    };

public:
    Frob() {
        a = 1;

        Bozo bozo;
    }
};

int main()
{
    // Good: the two shared_ptr's share the same object
    std::shared_ptr<Good> gp1 = std::make_shared<Good>();
    std::shared_ptr<Good> gp2 = gp1->getptr();
    std::cout << "gp2.use_count() = " << gp2.use_count() << '\n';
    std::cout << "gp1.use_count() = " << gp1.use_count() << '\n';
    Frob f;
 
    // Bad: shared_from_this is called without having std::shared_ptr owning the caller 
    try {
        Good not_so_good;
        std::shared_ptr<Good> gp1 = not_so_good.getptr();
    } catch(std::bad_weak_ptr& e) {
        // undefined behavior (until C++17) and std::bad_weak_ptr thrown (since C++17)
        std::cout << e.what() << '\n';    
    }
 
    // Bad, each shared_ptr thinks it's the only owner of the object
    std::shared_ptr<Bad> bp1 = std::make_shared<Bad>();
    std::shared_ptr<Bad> bp2 = bp1->getptr();
    std::cout << "bp2.use_count() = " << bp2.use_count() << '\n';
} // UB: double-delete of Bad
