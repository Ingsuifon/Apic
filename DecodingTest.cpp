//
// Created by Borelset on 2023/8/16.
//

#include <iostream>
#include <fstream>
#include <set>
#include <sys/time.h>
#include <unordered_map>
#include "IntegerConvertor.h"
#include "gflags/gflags.h"
#include "rapidcsv.h"
#include "FileOperator.h"
#include "AthCompressor.h"

DEFINE_int32(DatumSize, 9, "batch size");
DEFINE_int32(Mode, 0, "mode");

struct Result{
    uint32_t value;
    int length;
    uint8_t encode[8];
};

//struct State{
//    bool end;
//    uint32_t value;
//    int length;
//    PrebuildState prebuildState;
//};
//
//struct HeadNode{
//    bool end;
//    uint32_t value;
//    std::unordered_map<uint8_t, State> table;
//};

struct ValueListEntry{
    int commonLength;
    std::set<uint32_t> values;
};

void SaveCheckTable(const std::unordered_map<uint8_t, HeadNode>& checkTable){
  std::string Path = "CheckTable_S" + std::to_string(FLAGS_DatumSize) + "_M" + std::to_string(FLAGS_Mode);
  FileOperator fileOperator((char*)Path.data(), FileOpenType::ReadWrite);
  std::string result;

  std::string checkTableContent;
  int count = 0;
  for(const auto& entry: checkTable){
    checkTableContent.append((char*)&(entry.first), sizeof(entry.first));
    checkTableContent.append((char*)&(entry.second.end), sizeof(entry.second.end));
    checkTableContent.append((char*)&(entry.second.value), sizeof(entry.second.value));
    count++;
  }
  std::string checkTableHeader;
  checkTableHeader.append((char*)&count, sizeof(count));

  result = checkTableHeader + checkTableContent;

  for(const auto& entry: checkTable){
    std::string headNodeHeader, headNodeContent;
    int valueCount = 0;
    for(const auto& value: entry.second.table){
      headNodeContent.append((char*)&(value.first), sizeof(value.first));
      headNodeContent.append((char*)&(value.second), sizeof(value.second));
      valueCount++;
    }
    headNodeHeader.append((char*)&valueCount, sizeof(valueCount));
    headNodeHeader.append((char*)&entry.first, sizeof(entry.first));
    result += headNodeHeader + headNodeContent;
  }

  uint32_t writesize = fileOperator.write((uint8_t*)result.data(), result.size());
  fileOperator.fsync();
  fileOperator.releaseBufferedData();
  printf("Saved... File Size: %u\n", writesize);
}

void LoadCheckTable(std::unordered_map<uint8_t, HeadNode>& checkTable){
  std::string Path = "CheckTable_S" + std::to_string(FLAGS_DatumSize) + "_M" + std::to_string(FLAGS_Mode);
  FileOperator fileOperator((char*)Path.data(), FileOpenType::Read);
  uint32_t size = FileOperator::size(Path);
  printf("Read... File Size: %u\n", size);
  char* result = (char*)malloc(size);
  fileOperator.read((uint8_t*)result, size);
  uint32_t pos = 0;

  int count = 0;
  memcpy(&count, result+pos, sizeof(count));
  pos += sizeof(count);
  for(int i=0; i<count; i++){
    uint8_t key;
    HeadNode value;
    memcpy(&key, result+pos, sizeof(key));
    pos += sizeof(key);
    memcpy(&(value.end), result+pos, sizeof(value.end));
    pos += sizeof(value.end);
    memcpy(&(value.value), result+pos, sizeof(value.value));
    pos += sizeof(value.value);
    checkTable[key] = value;
  }

  for(int i=0; i<count; i++){
    int valueCount = 0;
    uint8_t uk = 0;
    memcpy(&valueCount, result+pos, sizeof(valueCount));
    pos += sizeof(valueCount);
    memcpy(&uk, result+pos, sizeof(uk));
    pos += sizeof(uk);
    HeadNode& headNode = checkTable[uk];
    for(int j=0; j<valueCount; j++){
      uint8_t key;
      State value;
      memcpy(&key, result+pos, sizeof(key));
      pos += sizeof(key);
      memcpy(&value, result+pos, sizeof(value));
      pos += sizeof(value);
      headNode.table[key] = value;
    }
  }
}

int main(int argc, char **argv) {

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  uint64_t maxValue = 1;
  for(int i=0; i<FLAGS_DatumSize; i++){
    maxValue = maxValue << 1;
  }

  std::map<uint8_t, std::vector<Result>> tables;  // positive order table
  std::unordered_map<uint8_t, HeadNode> checkTable; // reverse table

  // build positive order table (value -> encoded)
  for(uint32_t testValue=0; testValue<maxValue; testValue++){
    EncodeOutput encodeOutput = AthEncoding(testValue, FLAGS_DatumSize, FLAGS_Mode);
//    printf("Value:%u, String:", testValue);
//    for(int i=0; i<encodeOutput.length; i++){
//      printf("%x ", encodeOutput.output[i]);
//    }
//    printf("\n");


    Result result;
    result.length = encodeOutput.length;
    for(int i=0; i<result.length; i++){
      result.encode[i] = encodeOutput.output[i];
    }
    result.value = testValue;

    std::vector<Result>& currentSubTable = tables[encodeOutput.output[0]];
    currentSubTable.push_back(result);
  }

  // process cases with different head bytes for "Result" in "tables"
  for(auto& tt: tables){
    std::map<uint32_t, ValueListEntry> valueList;
    for(auto& item: tt.second){
      if(item.length > 1){
        valueList[item.encode[1]].values.insert(item.value);
      }
    }

    for(auto& entry: valueList){
      if(entry.second.values.size() <= 1) continue;

      // check the case of producing states
      int commonLength = 0;
      int bit = 0;
      bool first = true;
      for(int i=0; i<FLAGS_DatumSize; i++){
        uint32_t mask = 0x01 << i;
        for(auto& item: entry.second.values){
          int currentBit = ((mask & item) >> i);
          if(first) {
            bit = currentBit;
            first = false;
          }else{
            if(currentBit != bit) {
              commonLength = i;
              break;
            }
          }
        }
        first = true;
        if(commonLength) break;
      }
      assert(commonLength != 0);
      entry.second.commonLength = commonLength;
    }

    // build prebuild table
    for(auto& item: tt.second){
      HeadNode& headNode = checkTable[item.encode[0]];
      if(item.length == 1){
        headNode.end = true;
        headNode.value = item.value;
      }else{
        headNode.end = false;
        State& state = headNode.table[item.encode[1]];
        ValueListEntry& valueListEntry = valueList[item.encode[1]];
        if(valueListEntry.values.size() > 1){
          PrebuildState prebuildState = AthDecoding_prebuild((uint8_t*)&(item.encode), FLAGS_DatumSize, 2, valueListEntry.commonLength, FLAGS_Mode);
          state.end = false;
          state.prebuildState = prebuildState;
        }else{
          DecodeOutput decodeOutput = AthDecoding((uint8_t*)&(item.encode), FLAGS_DatumSize, FLAGS_Mode);
          state.length = decodeOutput.usedLength;
          state.end = true;
          state.value = item.value;
        }
      }
    }
  }

  // save prebuild table
  SaveCheckTable(checkTable);
  std::unordered_map<uint8_t, HeadNode> checkTable2;
  LoadCheckTable(checkTable2);

  // check correctness
  uint64_t duration = 0;
  for(uint32_t testValue=0; testValue<maxValue; testValue++){
    EncodeOutput encodeOutput = AthEncoding(testValue, FLAGS_DatumSize, FLAGS_Mode);

    uint32_t result = 0;

    timeval t0, t1;
    gettimeofday(&t0, NULL);
    HeadNode& headNode = checkTable2[encodeOutput.output[0]];
    if(headNode.end){
      result = headNode.value;
    }else{
      State& state = headNode.table[encodeOutput.output[1]];
      if(state.end){
        result = state.value;
      }else{
        DecodeOutput decodeOutput = AthDecoding_fast(&(encodeOutput.output[state.prebuildState.usedLength]), FLAGS_DatumSize, state.prebuildState, FLAGS_Mode);
        memcpy(&result, decodeOutput.output, sizeof(uint32_t));
      }
    }
    gettimeofday(&t1, NULL);
    duration += (t1.tv_sec - t0.tv_sec)*1000000 + t1.tv_usec - t0.tv_usec;
    assert(result == testValue);
  }

  printf("Num: %lu, Time: %lu us, Speed: %lu nums/s\n", maxValue, duration, maxValue * 1000000 / duration );


}