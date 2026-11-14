int main(void) {
 unsigned count=0; const unsigned table[]={0,0,1,2,1,0,0,0};
 for(unsigned size=0;size<4;++size) for(unsigned ext=0;ext<2;++ext)
 for(unsigned amx=0;amx<8;++amx) {
   uint32_t bits=(size<<7)|(ext<<6)|(amx<<3);
   unsigned want=size?0:table[amx];
   assert(select_address_route(bits)==want);
   assert(select_address_route(bits|0xfffffe07u)==want); count+=2;
 }
 assert(count==128);
 assert(select_address_route(0xc00a0e24)==ADDRESS_PHYSICAL);
 assert(select_address_route(0xc00a0e1c)==ADDRESS_TRANSLATED);
 for(unsigned dram=0;dram<8;++dram) {
   bool ok=dram==2 || dram==3;
   assert(address_profile_supported(dram<<2,0x20,0x01000000)==ok);
   assert(!address_profile_supported(dram<<2,0x20,0x02000000));
   assert(address_profile_supported(dram<<2,0x10,0x01000000)==ok);
   assert(address_profile_supported(dram<<2,0x10,0x02000000)==ok);
   assert(address_profile_supported(dram<<2,0x18,0x01000000)==ok);
   assert(address_profile_supported(dram<<2,0x18,0x02000000)==ok);
   assert(!address_profile_supported(dram<<2,0x18,0x04000000));
   assert(!address_profile_supported(dram<<2,0x98,0x01000000));
   assert(!address_profile_supported(dram<<2,0,0x01000000));
 }
 puts("AMX_SELECTOR source-extracted 130 route + 72 profile cases PASS");
 return 0;
}
