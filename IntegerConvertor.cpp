//
// Created by Borelset on 2023/7/13.
//

#include "IntegerConvertor.h"

#include <sys/time.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_map>

#include "AthCompressor.h"
#include "FileOperator.h"
#include "gflags/gflags.h"
#include "rapidcsv.h"

DEFINE_string(InputFile, "", "batch process file path");
DEFINE_int32(BatchSize, 20, "batch size");
DEFINE_string(Columns, "", "batch process file path");

std::set<int> parser(std::string& columns) {
  std::set<int> result;
  std::string leftStr = columns;
  while (leftStr.size() != 0) {
    std::size_t pos = leftStr.find(',');
    if (pos == std::string::npos) {
      int colNumber = std::stoi(leftStr);
      result.insert(colNumber);
      break;
    } else {
      std::string numberStr = leftStr.substr(0, pos);
      int colNumber = std::stoi(numberStr);
      result.insert(colNumber);
      leftStr = leftStr.substr(pos + 1);
    }
  }

  return result;
}

std::unordered_map<
    uint8_t, std::unordered_map<uint8_t, std::unordered_map<uint8_t, HeadNode>>>
    checkTableList;

void LoadCheckTable(std::unordered_map<uint8_t, HeadNode>& checkTable,
                    int bitwidth, uint8_t mode) {
  std::string Path = "./DST/CheckTable_S" + std::to_string(bitwidth) + "_M" +
                     std::to_string(mode);
  FileOperator fileOperator((char*)Path.data(), FileOpenType::Read);
  uint32_t size = FileOperator::size(Path);
  // printf("Read... File Size: %u\n", size);
  char* result = (char*)malloc(size);
  fileOperator.read((uint8_t*)result, size);
  uint32_t pos = 0;

  int count = 0;
  memcpy(&count, result + pos, sizeof(count));
  pos += sizeof(count);
  for (int i = 0; i < count; i++) {
    uint8_t key;
    HeadNode value;
    memcpy(&key, result + pos, sizeof(key));
    pos += sizeof(key);
    memcpy(&(value.end), result + pos, sizeof(value.end));
    pos += sizeof(value.end);
    memcpy(&(value.value), result + pos, sizeof(value.value));
    pos += sizeof(value.value);
    checkTable[key] = value;
  }

  for (int i = 0; i < count; i++) {
    int valueCount = 0;
    uint8_t uk = 0;
    memcpy(&valueCount, result + pos, sizeof(valueCount));
    pos += sizeof(valueCount);
    memcpy(&uk, result + pos, sizeof(uk));
    pos += sizeof(uk);
    HeadNode& headNode = checkTable[uk];
    for (int j = 0; j < valueCount; j++) {
      uint8_t key;
      State value;
      memcpy(&key, result + pos, sizeof(key));
      pos += sizeof(key);
      memcpy(&value, result + pos, sizeof(value));
      pos += sizeof(value);
      headNode.table[key] = value;
    }
  }
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::cout << "Batch path: " << FLAGS_InputFile << std::endl;

  for (uint8_t i = 0; i < 8; i++) {
    std::unordered_map<uint8_t, std::unordered_map<uint8_t, HeadNode>>&
        modeCheckTable = checkTableList[i];
    for (int j = 9; j < 27; j++) {
      LoadCheckTable(modeCheckTable[j], j, i);
    }
  }

  std::set<int> colNumbers = parser(FLAGS_Columns);

  rapidcsv::Document doc(FLAGS_InputFile);

  std::vector<std::vector<int64_t>> data;

  for (auto item : colNumbers) {
    data.push_back(doc.GetColumn<int64_t>(item));
  }

  uint64_t originalSize = 0;
  for (int i = 0; i < data.size(); i++) {
    int64_t max_v = *std::max_element(data[i].begin(), data[i].end());
    size_t bytes = 0;
    while (max_v > 0) {
      max_v >>= 8;
      bytes++;
    }
    originalSize += bytes * data[i].size();
  }

  uint64_t count = data.begin()->size();
  uint64_t batchNum = count / FLAGS_BatchSize;
  uint64_t compressionCost = 0;
  uint64_t decompressionCost = 0, decompressionCost2 = 0;
  uint64_t decompressionNum = 0;

  ComData comDataTotal;
  for (int i = 0; i < batchNum; i++) {
    for (int j = 0; j < colNumbers.size(); j++) {
      AthCompressor athCompressor;
      for (int k = 0; k < FLAGS_BatchSize; k++) {
        int index = i * FLAGS_BatchSize + k;
        if (index >= count) break;
        athCompressor.addData(data[j][index]);
      }
      std::string result;
      struct timeval t0, t1;
      gettimeofday(&t0, nullptr);
      ComData comData = athCompressor.compress(&result);
      gettimeofday(&t1, nullptr);
      compressionCost +=
          (t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec;
      comDataTotal = comDataTotal + comData;

      int datumBit = athCompressor.getDatumBits();
      uint8_t mode = athCompressor.getMode();

      if (athCompressor.getValid()) {
        uint8_t* ptr = (uint8_t*)result.data();
        for (int k = 0; k < FLAGS_BatchSize; k++) {
          int index = i * FLAGS_BatchSize + k;
          if (index >= count) break;

          std::unordered_map<uint8_t, std::unordered_map<uint8_t, HeadNode>>&
              modeCheckTable = checkTableList[mode];
          std::unordered_map<uint8_t, HeadNode>& checkTable =
              modeCheckTable[datumBit];

          gettimeofday(&t0, nullptr);
          DecodeOutput decodeOutput = AthDecoding(ptr, datumBit, mode);
          gettimeofday(&t1, nullptr);
          decompressionCost +=
              (t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec;
          decompressionNum++;
          uint64_t value = 0;
          memcpy(&value, decodeOutput.output, decodeOutput.length);

          uint32_t result_fast = 0;
          int length = 0;
          gettimeofday(&t0, nullptr);
          HeadNode& headNode = checkTable[ptr[0]];
          if (headNode.end) {
            result_fast = headNode.value;
            length = 1;
          } else {
            State& state = headNode.table[ptr[1]];
            if (state.end) {
              result_fast = state.value;
              length = state.length;
            } else {
              DecodeOutput decodeOutput =
                  AthDecoding_fast(&(ptr[state.prebuildState.usedLength]),
                                   datumBit, state.prebuildState, mode);
              memcpy(&result_fast, decodeOutput.output, sizeof(uint32_t));
              length = decodeOutput.usedLength;
            }
          }
          gettimeofday(&t1, nullptr);
          decompressionCost2 +=
              (t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec;

          assert(result_fast == value);
          assert(length == decodeOutput.usedLength);

          ptr += decodeOutput.usedLength;

          int64_t recover = athCompressor.getDeltaMin();
          recover += value;
          if (recover != data[j][index]) {
            printf("#%d %ld => %ld\n", k, data[j][index], recover);
          }
        }
      }
    }
  }

  std::ofstream out("result.csv", std::ios::app);

  double cpr1 = static_cast<double>(originalSize) / comDataTotal.naive;
  double cpr2 = static_cast<double>(originalSize) / comDataTotal.ath;
  double improvement = (cpr2 - cpr1) / cpr1 * 100;
  uint64_t cmpr_speed = count * 1000000 / compressionCost;
  uint64_t decmpr_speed1 = count * data.size() * 1000000 / decompressionCost;
  uint64_t decmpr_speed2 = count * data.size() * 1000000 / decompressionCost2;

  out << improvement << ',' << cmpr_speed << ',' << decmpr_speed1 << ','
      << decmpr_speed2 << std::endl;
  std::cout << "Cmpr: " << cpr2 << ", Cmpr_Speed: " << cmpr_speed
            << ", Decmpr_Speed1: " << decmpr_speed1 << " nums/s"
            << ", Decmpr_Speed2: " << decmpr_speed2 << " nums/s" << '\n' << std::endl;
}