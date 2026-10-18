// SPDX-License-Identifier: GPL-2.0-or-later
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <vector>
extern "C" {
#include "keyboards/era/common/system/era_rp2040_matrix_pio_recovery.h"
}

namespace {
const std::array<unsigned,6> left_rows{24,8,26,25,27,28}, right_rows{29,28,27,26,11,3};
const std::array<unsigned,9> left_cols{29,9,7,6,5,4,3,2,32}, right_cols{25,24,23,22,7,6,5,4,2};
struct Model {
    std::array<unsigned,6> rows;
    std::array<unsigned,9> cols;
    std::array<uint16_t,6> keys{};
    std::array<uint32_t,9> recovery_until{};
    uint16_t code[32]{};
    size_t length;
    uint32_t row_mask=0, matrix_mask=0, low=0, time=0, recovery=0;
    uint32_t osr=0,x=0,y=0,stuck=0;
    unsigned pc=0,pulls=0;
    std::vector<uint32_t> drive_times;
    std::vector<uint16_t> samples;
    Model(bool right, uint32_t settle=256) : rows(right?right_rows:left_rows),cols(right?right_cols:left_cols) {
        for(auto pin:rows)row_mask|=1U<<pin;
        matrix_mask=row_mask;
        for(auto pin:cols)if(pin<30)matrix_mask|=1U<<pin;
        length=era_rp2040_matrix_pio_recovery_encode(code,32,settle,64,matrix_mask);
    }
    uint32_t pins() {
        uint32_t v=~low & ~stuck;
        for(unsigned c=0;c<9;++c)if(cols[c]<30) {
            bool pressed=time<recovery_until[c];
            for(unsigned r=0;r<6;++r)pressed|=bool(low&(1U<<rows[r])) && bool(keys[r]&(1U<<c));
            if(pressed)v&=~(1U<<cols[c]);
        }
        return v;
    }
    void output(uint32_t value) {
        uint32_t next=~value & row_mask;
        for(unsigned r=0;r<6;++r)if((low & ~next)&(1U<<rows[r]))
            for(unsigned c=0;c<9;++c)if(keys[r]&(1U<<c))recovery_until[c]=time+recovery;
        low=next;
    }
    void run(unsigned slots=16) {
        ASSERT_GT(length,0U);
        for(unsigned step=0;step<100000 && pulls<=slots;++step) {
            uint16_t word=code[pc], op=word&~0x1F00U;
            unsigned next=pc+1, bits=op&31;
            if(op==0x80A0) {
                if(pulls==slots)return;
                osr=pulls%8<6?~(1U<<rows[pulls%8]):~0U;
                ++pulls;
            } else if(op==0x6000) {
                output(osr);osr=0;drive_times.push_back(time);
            } else if(op==0x4000) {
                uint16_t sample=0;auto v=pins();
                for(unsigned c=0;c<9;++c)if(cols[c]<30 && !(v&(1U<<cols[c])))sample|=1U<<c;
                samples.push_back(sample);
            } else if(op==0xA00B)output(~0U);
            else if((op&0xFFE0)==0xE020)x=bits;
            else if(op==0xE05F)y=31;
            else if(op==0xA0E8)osr=~pins();
            else if(op==0xA042) {}
            else if((op&0xFFE0)==0x6060)osr>>=bits;
            else if((op&0xFFE0)==0x6020) {x=osr&((1U<<bits)-1);osr>>=bits;}
            else if((op&0xE000)==0) {
                unsigned cond=(op>>5)&7;
                if(cond==0 || (cond==1 && !x))next=bits;
                else if(cond==2) {bool jump=x!=0;--x;if(jump)next=bits;}
                else if(cond==4) {bool jump=y!=0;--y;if(jump)next=bits;}
                else ASSERT_EQ(cond,1U);
            } else FAIL()<<"Unexpected opcode "<<std::hex<<op;
            time+=1+((word>>8)&31);
            pc=next==length?0:next;
        }
        FAIL()<<"Unbounded recovery or broken pattern stream";
    }
};
}

TEST(EraMatrixRecovery, IdleCostAndUnrelatedInputs) {
    for(uint32_t settle:{128U,256U})for(bool right:{false,true}) {
        Model m(right,settle);m.stuck=~m.matrix_mask;m.run();
        ASSERT_EQ(m.samples.size(),16U);
        for(auto sample:m.samples)EXPECT_EQ(sample,0);
        for(size_t i=1;i<m.drive_times.size();++i)
            EXPECT_EQ(m.drive_times[i]-m.drive_times[i-1],settle+(right?79U:76U));
    }
}

TEST(EraMatrixRecovery, ContactRecoveryAndSimultaneousKeysArePreserved) {
    for(uint32_t settle:{128U,256U})for(bool right:{false,true})for(uint32_t recovery:{0U,100U,300U,1000U}) {
        Model m(right,settle);m.recovery=recovery;
        m.keys={8,8,0x11,8,8,0x80};
        m.run(24);
        ASSERT_EQ(m.samples.size(),24U);
        for(unsigned i=0;i<24;++i) {
            unsigned r=i%8;
            auto mask=right?0x1FFU:0xFFU;
            EXPECT_EQ(m.samples[i],r<6?(m.keys[r]&mask):0U)<<"right="<<right<<" recovery="<<recovery<<" slot="<<i;
        }
    }
}

TEST(EraMatrixRecovery, StuckColumnsHaveBoundedWaitNotMaskedSamples) {
    for(uint32_t settle:{128U,256U})for(bool right:{false,true}) {
        Model m(right,settle);m.stuck=1U<<m.cols[3];m.run();
        for(auto sample:m.samples)EXPECT_EQ(sample,8);
        for(size_t i=1;i<m.drive_times.size();++i)
            EXPECT_LT(m.drive_times[i]-m.drive_times[i-1],3750U); // <30us at 125MHz
    }
}

TEST(EraMatrixRecovery, ProgramStorageAndInvalidMasksAreRejected) {
    Model left(false),right(true);
    EXPECT_LE(left.length,32U);EXPECT_LE(right.length,32U);
    uint16_t code[32];
    EXPECT_EQ(era_rp2040_matrix_pio_recovery_encode(code,left.length-1,256,64,left.matrix_mask),0U);
    EXPECT_EQ(era_rp2040_matrix_pio_recovery_encode(code,32,256,64,0),0U);
    EXPECT_EQ(era_rp2040_matrix_pio_recovery_encode(code,32,256,64,1U<<31),0U);
}
