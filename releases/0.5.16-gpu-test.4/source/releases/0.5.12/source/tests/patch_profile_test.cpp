#include <string>
#include <Windows.h>
#include "../src/client_1_0_profile.h"
#include <iostream>
#include <iterator>
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary); std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),{});
 auto path=std::filesystem::temp_directory_path()/("minimap-profile-"+std::to_string(GetCurrentProcessId())+".exe");
 auto check=[&](const char* name,bool expected){std::ofstream o(path,std::ios::binary);o.write((char*)b.data(),b.size());o.close();bool got=MinimapClient10::MatchesFile(path);std::filesystem::remove(path);if(got!=expected){std::cerr<<"FAIL "<<name<<'\n';exit(1);}std::cout<<"PASS "<<name<<'\n';};
 check("baseline",true); auto original=b;
 DWORD pe=0;memcpy(&pe,b.data()+60,4);
 b[pe+8]^=1;check("timestamp-only patch",true);b=original;
 b[pe+24+64]^=1;check("checksum-only patch",true);b=original;
 b[4096]^=1;check("code change rejected",false);b=original;
 b[pe+24+16]^=1;check("entrypoint change rejected",false);b=original;
 b[60]=255;b[61]=255;b[62]=255;b[63]=127;check("invalid PE offset rejected",false);b=original;
 b.resize(100);check("truncated image rejected",false);
}
