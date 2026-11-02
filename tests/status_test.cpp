#include "status_text.hpp"
#include <cstdlib>
#include <iostream>
void check(bool condition) {
    if (!condition) {
        std::cerr << "status parser failed\n";
        std::exit(1);
    }
}
int main() {
    using namespace ppwm;
    auto plain = parse_status("CPU 2% | 13:00");
    check(plain.size() == 1 && plain[0].text == "CPU 2% | 13:00");
    auto rich = parse_status("^c#a0a0a0^^r0,6,16,2r0,16,16,2f22^98%^d^");
    check(rich.size() == 6 && rich[0].kind == StatusKind::Foreground);
    check(rich[1].kind == StatusKind::Rectangle && rich[2].y == 16);
    check(rich[3].kind == StatusKind::Offset && rich[3].x == 22);
    check(rich[4].text == "98%" && rich[5].kind == StatusKind::Reset);
    check(parse_status("a^^b")[0].text == "a^b");
    check(parse_status("a^unfinished")[0].text == "a^unfinished");
    check(parse_status("^r1,x,2,3f99999999999999999999^ok").back().text == "ok");
    check(parse_status("^c#xxxxxx^ok").front().text == "ok");
    check(parse_status(std::string(10000, 'x'))[0].text.size() == 4096);
    check(parse_status(std::string("a\1b", 3))[0].text == "ab");
    std::cout << "Status parser passed\n";
}
