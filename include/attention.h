#ifndef _ATTENTION_H_   
#define _ATTENTION_H_

#include "runtime.h"
#include "wsqueue.h"
#include <llvm/ADT/SmallVector.h>

enum AttentionFuncType:bool{
    ATTENTION_SPAWN = 0,
    ATTENTION_SYNC = 1,
};

struct AttentionParams {
    static constexpr int seqLength = 8;
    static constexpr int headDim   = 4;
    static constexpr int TS = 4;
    static constexpr int TD = 2;

    float attentionScore; 
    float tiledOutput[TS][TD];

    llvm::SmallVector<int, seqLength * headDim> qFlat;
    llvm::SmallVector<int, seqLength * headDim> kFlat;
    llvm::SmallVector<int, seqLength * headDim> vFlat;

    int qTile[TS][TD];
    int kTile[TS][TD];
    int vTile[TS][TD];

    AttentionParams() {
        qFlat.resize(seqLength * headDim);
        kFlat.resize(seqLength * headDim);
        vFlat.resize(seqLength * headDim);

        for (int i = 0; i < seqLength; ++i) {
            for (int j = 0; j < headDim; ++j) {
                int idx = i * headDim + j;
                qFlat[idx] = 1000 + idx;
                kFlat[idx] = 2000 + idx;
                vFlat[idx] = 3000 + idx;
            }
        }

        tile(qFlat, qTile);
        tile(kFlat, kTile);
        tile(vFlat, vTile);
    }

private:
    void tile(const llvm::SmallVector<int, seqLength * headDim> &data,
              int tiled[TS][TD]) {

        for (int i = 0; i < TS; ++i) {
            for (int j = 0; j < TD; ++j) {
                tiled[i][j] = data[i * headDim + j];
            }
        }
    }
};

struct AttentionArgs {
    AttentionParams params; 
    Worker<AttentionArgs, AttentionFuncType>::Task* address;
};


#endif //_ATTENTION_H_