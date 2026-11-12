/* Original synthetic stream builder shared by transport tests. */
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(24-i*8));
}
static uint8_t *stream(unsigned hdr,unsigned tail,unsigned ch,unsigned start,unsigned end,size_t *size) {
    *size=hdr+3*18*ch+tail;
    uint8_t *p=calloc(*size+1,1); if(!p) exit(2);
    p[0]=0x80;p[2]=(uint8_t)((hdr-4)>>8);p[3]=(uint8_t)(hdr-4);
    p[4]=3;p[5]=18;p[6]=4;p[7]=(uint8_t)ch;put32(p+8,44100);put32(p+12,65);
    p[16]=1;p[17]=0xf4;p[18]=3;
    put32(p+20,1);put32(p+24,1);put32(p+28,start);
    put32(p+32,hdr+start/32*18*ch);put32(p+36,end);put32(p+40,hdr+(end+31)/32*18*ch);
    memcpy(p+hdr-6,"(c)CRI",6);
    for(unsigned g=0;g<3;++g)for(unsigned c=0;c<ch;++c) {
        uint8_t *b=p+hdr+(g*ch+c)*18;b[1]=(uint8_t)(c+1);
        for(unsigned i=2;i<18;++i)b[i]=(uint8_t)(i*7+g*3+c*19);
    }
    uint8_t *f=p+*size-tail;f[0]=0x80;f[1]=1;f[2]=(uint8_t)((tail-4)>>8);f[3]=(uint8_t)(tail-4);
    return p;
}
