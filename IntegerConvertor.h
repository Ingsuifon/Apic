//
// Created by Borelset on 2023/7/13.
//

#ifndef SIMBASEDREORDER_INTEGERCONVERTOR_H
#define SIMBASEDREORDER_INTEGERCONVERTOR_H

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <cassert>
#include <cstring>
#include <map>
#include "AthEncoding.h"

struct ICNode{
    int64_t data;
    int order;
};

struct Output{
    int64_t delta;
    int order;
};

struct Info{
    uint8_t orderLength:4;
    uint8_t datumLength:4;
};

int CheckBits(uint64_t value, int datum){
  int result = 0;
  for(int i=0; i<datum*8; i++){
    if((value & 0x01) == 1){
      result++;
    }
    value = value >> 1;
  }
  return result;
}

class LinearSolver {
public:
    LinearSolver(int num, const ICNode *list, const int *refList) : dataNumber(num){
      double sumx2 = 0.0, sumxy = 0.0, sumx = 0.0, sumy = 0.0;
      int refNumber = 0, index = -1;
      for(int i=0; i<dataNumber; i++){
        if(refList[i]){
          refNumber++;
          index = i;
          sumx2 += (double)i * i;
          sumxy += (double)list[i].data * i;
          sumx  += (double)i;
          sumy  += (double)list[i].data;
        }
      }
      if(refNumber == 1){
        para_b = 0;
        para_k = list[index].data;
        return;
      }

      para_k = (refNumber * sumxy - sumx * sumy) / (refNumber * sumx2 - sumx * sumx);
      para_b = sumy/refNumber - para_k * sumx / refNumber;
    }

    float getK(){
      return para_k;
    }

    float getB(){
      return para_b;
    }

    float predict(int x){
      return para_k * x + para_b;
    }
private:
    int dataNumber;
    float para_k, para_b;
};

bool operator<(const ICNode& lh, const ICNode& rh){
  return lh.data < rh.data;
}

class IntegerConvertor {
public:
    IntegerConvertor(){

    }

    void addData(int64_t datum){
      dataList.push_back(datum);
      intermediates.push_back({datum, number++});
    }

    int convert(){
      if(intermediates.size() <= 1) return 0;
      sort();
      checkLinearRef();
      linearSolver();
      exception();
    }

    int getSize(){
      int64_t max = INT64_MIN, min = INT64_MAX;
      for(int i=0; i<dataList.size(); i++){
        if(dataList[i] > max) max = dataList[i];
        if(dataList[i] < min) min = dataList[i];
      }

      uint64_t delta = max - min;
      int level = bitLength(delta);

      int main = level / 8;
      int tail = level % 8;
      if(tail){
        target = main + 1;
      }else{
        target = main;
      }

      int result = 4 + target * dataList.size();

      uint64_t bit1 = 0, bit0 = 0;
      uint64_t currentLength = 4;
      for(int i=0; i<dataList.size(); i++){
        uint64_t temp = dataList[i];
        temp -= min;
        EncodeOutput encodeOutput = AthEncoding(temp, target, 0);
        currentLength += encodeOutput.length;
//        for(int j=0; j<target*8; j++){
//          if((temp & 0x01) == 1){
//            bitRecord[j]++;
//          }
//          temp = temp >> 1;
//        }

//        int rr = CheckBits(temp, target);
//        bit1 += rr;
//        bit0 += target * 8 - rr;
      }
//      printf("Target:%d/%d, Bit1: %lu, Bit0: %lu, MaxBit1:%d, MaxBit0:%d\n", target, level, bit1, bit0, CheckBits(delta, target), target*8 - CheckBits(delta, target));
//
//      for(int i=0; i<target * 8; i++) {
//        printf("#%d: %d ", i, bitRecord[i]);
//      }
//      printf("\n");
      printf("Old:%d, New:%lu\n", result, currentLength);

      return result;
    }

    int getTarget(){
      return target;
    }

    int getDatum(){
      return datumSize;
    }

    std::string toString(){
      std::string result;
      result.append((char*)&k, sizeof(k));
      result.append((char*)&b, sizeof(b));
      result.append((char*)&deltaBase, sizeof(deltaBase));

      int datumLength = (orderWidth + deltaWidth)/8;
      if((orderWidth+deltaWidth)%8) datumLength++;
      datumSize = datumLength;

      Info info;
      info.orderLength = orderWidth;
      info.datumLength = datumLength;

      result.append((char*)&info, sizeof(info));

      for(int i=0; i<number; i++){
        uint64_t order, delta;
        order = outputList[i].order;
        order = order << (datumLength * 8 - orderWidth);
        delta = outputList[i].delta;
        uint64_t temp = order + delta;
        result.append((char*)&temp, datumLength);
      }

      return result;
    }

    static std::vector<int64_t> toData(std::string str){
      char* data_ptr = (char*)str.data();

      float* k_ptr = (float*)data_ptr;
      data_ptr += sizeof(float);
      float* b_ptr = (float*)data_ptr;
      data_ptr += sizeof(float);
      int32_t* base_ptr = (int32_t*)data_ptr;
      data_ptr += sizeof(int32_t);

      Info* info_ptr = (Info*)data_ptr;
      data_ptr += sizeof(Info);

      int leftLength = str.size() - sizeof(float) - sizeof(float) - sizeof(uint32_t) - sizeof(Info);
      int number = leftLength / info_ptr->datumLength;
      assert(leftLength % info_ptr->datumLength == 0);

      std::vector<int64_t> result;

      for(int i=0; i<number; i++){
        uint64_t temp = 0;
        memcpy(&temp, data_ptr, info_ptr->datumLength);
        int shift = info_ptr->datumLength * 8 - info_ptr->orderLength;
        uint64_t order = temp >> shift;
        shift = 64 - info_ptr->datumLength * 8 + info_ptr->orderLength;
        uint64_t delta = (temp << shift) >> shift;

        int64_t value = delta + (*base_ptr) + lround((*k_ptr) * order + (*b_ptr));
        result.push_back(value);

        data_ptr += info_ptr->datumLength;
      }

      return result;
    }

private:

    void sort(){
      std::sort(intermediates.begin(), intermediates.end());
    }

    void checkLinearRef(){
      linearRefList.push_back(1);
      for(int i=1; i<intermediates.size(); i++){
        if(intermediates[i].data == intermediates[i-1].data){
          linearRefList.push_back(0);
        }else{
          linearRefList.push_back(1);
        }
      }

      int64_t intervalSize = INT64_MAX;
      int lastRefPos = -1;
      while(true){
        LinearSolver linearSolver(intermediates.size(), intermediates.data(), linearRefList.data());

        int absMaxPos = 0;
        uint64_t absMaxDelta = 0;
        int64_t max = INT64_MIN, min = INT64_MAX;
        int index = 0;
        for(int i=0; i<dataList.size(); i++){
          int64_t delta;
          if(linearRefList[i]){
            delta = intermediates[i].data - lround(linearSolver.predict(index));
            index++;
          }else{
            int64_t delta1 = intermediates[i].data - lround(linearSolver.predict(index));
            int64_t delta2 = intermediates[i].data - lround(linearSolver.predict(index-1));
            delta = std::min(delta1, delta2);
          }

          if(std::abs(delta) > absMaxDelta) {
            absMaxDelta = std::abs(delta);
            absMaxPos = i;
          }
          if(delta > max) max = delta;
          if(delta < min) min = delta;
        }
        if(intervalSize > max-min){
          linearRefList[absMaxPos] = 0;
          lastRefPos = absMaxPos;
          intervalSize = max-min;
        }else{
          linearRefList[lastRefPos] = 1;
          break;
        }
      }
    }

    void linearSolver(){
      LinearSolver linearSolver(intermediates.size(), intermediates.data(), linearRefList.data());
      k = linearSolver.getK();
      b = linearSolver.getB();

      int index = 0;
      outputList.reserve(number);
      int64_t min = INT64_MAX, max = INT64_MIN;
      for(int i=0; i<intermediates.size(); i++){
        if(linearRefList[i]){
          int64_t delta = intermediates[i].data - lround(linearSolver.predict(index));
          outputList[intermediates[i].order] = {delta, index};
          index++;
          if(delta < min) min = delta;
          if(delta > max) max = delta;
        }else{
          int64_t delta1 = intermediates[i].data - lround(linearSolver.predict(index));
          int64_t delta2 = intermediates[i].data - lround(linearSolver.predict(index-1));
          if(std::abs(delta1) < std::abs(delta2)) {
            outputList[intermediates[i].order] = {delta1, index};
            if(delta1 < min) min = delta1;
            if(delta1 > max) max = delta1;
          }else {
            outputList[intermediates[i].order] = {delta2, index-1};
            if(delta2 < min) min = delta2;
            if(delta2 > max) max = delta2;
          }
        }
      }

      for(int i=0; i<number; i++){
        outputList[i].delta -= min;
      }

      orderWidth = bitLength(index);
      deltaWidth = bitLength(max - min);
      deltaBase = min;
    }

    void exception(){

    }

    int bitLength(uint64_t value){
      int level = 0;
      while(true){
        if((value >> level) != 0){
          level++;
        }else{
          break;
        }
      }
      if(level == 0) level = 1;
      return level;
    }

    int target;
    int number = 0;
    int orderWidth = 0, deltaWidth = 0;
    int datumSize = 0;
    float k = 0.0, b = 0.0;
    int32_t deltaBase = 0;
    std::vector<int64_t> dataList;
    std::vector<int> linearRefList;
    std::vector<ICNode> intermediates;
    std::vector<int64_t> exceptions;
    std::vector<Output> outputList;

    std::map<int, int> bitRecord;
};




#endif //SIMBASEDREORDER_INTEGERCONVERTOR_H
