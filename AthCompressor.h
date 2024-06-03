//
// Created by Borelset on 2023/7/13.
//

#ifndef SIMBASEDREORDER_ATHCOMPRESSOR_H
#define SIMBASEDREORDER_ATHCOMPRESSOR_H

#include <vector>

struct State {
  bool end;
  uint32_t value;
  int length;
  PrebuildState prebuildState;
};

struct HeadNode {
  bool end;
  uint32_t value;
  std::unordered_map<uint8_t, State> table;
};

extern std::unordered_map<
    uint8_t, std::unordered_map<uint8_t, std::unordered_map<uint8_t, HeadNode>>>
    checkTableList;

struct ComData {
  uint64_t naive = 0;
  uint64_t ath = 0;
};

ComData operator+(const ComData& lh, const ComData& rh) {
  ComData comData;
  comData.naive = lh.naive + rh.naive;
  comData.ath = lh.ath + rh.ath;
  return comData;
}

struct BitDistribution {
  int bit0 = 0;
  int bit1 = 0;
  int ratio = 0;
};

BitDistribution getBitDistribution(uint64_t value, int length) {
  BitDistribution bitDistribution;
  uint64_t temp = value;
  for (int j = 0; j < length * 8; j++) {
    if ((temp & 0x01) == 1) {
      bitDistribution.bit1++;
    } else {
      bitDistribution.bit0++;
    }
    temp = temp >> 1;
  }
  bitDistribution.ratio = 4096 * bitDistribution.bit1 /
                          (bitDistribution.bit0 + bitDistribution.bit1);

  return bitDistribution;
}

void getBitPos(uint64_t value, int bit, std::map<int, int>* bitmap) {
  uint64_t temp = value;
  for (int j = 0; j < bit; j++) {
    if ((temp & 0x01) == 1) {
      (*bitmap)[j]++;
    } else {
      (*bitmap)[j]--;
    }
    temp = temp >> 1;
  }
}

BitDistribution getBitDistribution2(uint64_t value, int bit) {
  BitDistribution bitDistribution;
  uint64_t temp = value;
  for (int j = 0; j < bit; j++) {
    if ((temp & 0x01) == 1) {
      bitDistribution.bit1++;
    } else {
      bitDistribution.bit0++;
    }
    temp = temp >> 1;
  }
  bitDistribution.ratio = 4096 * bitDistribution.bit1 /
                          (bitDistribution.bit0 + bitDistribution.bit1);

  return bitDistribution;
}

BitDistribution operator+(const BitDistribution& lh,
                          const BitDistribution& rh) {
  BitDistribution result;
  result.bit1 = lh.bit1 + rh.bit1;
  result.bit0 = lh.bit0 + rh.bit0;
  result.ratio = 4096 * result.bit1 / (result.bit0 + result.bit1);
  return result;
}

class AthCompressor {
 public:
  AthCompressor() {}

  void addData(int64_t data) {
    dataList.push_back(data);
    length++;
  }

  int getDatumBits() { return datumBit; }

  int getRatio() { return ratio; }

  int64_t getDeltaMin() { return deltaMin; }

  bool getValid() { return valid; }

  ComData compress(std::string* value) {
    minMax();
    calculateDatumSize();
    checkDistribution();
    ComData comData;
    *value = athEncodeWithoutMask();
    comData.ath = value->size();
    comData.naive = naiveEncode().size();
    return comData;
  }

  ComData compress2(std::string* value) {
    minMax();
    calculateDatumSize();
    checkDistribution();
    ComData comData;
    *value = athEncodeWithoutMask2();
    comData.ath = value->size();
    comData.naive = naiveEncode().size();
    return comData;
  }

  uint8_t getMode() { return mode; }

 private:
  void minMax() {
    int64_t min = INT64_MAX;
    for (int i = 0; i < length; i++) {
      if (dataList[i] < min) min = dataList[i];
    }
    deltaMin = min;

    int64_t max = INT64_MIN;
    for (int i = 0; i < length; i++) {
      int64_t value = dataList[i] - min;
      output.push_back(value);
      if (value > max) max = value;
    }
    deltaMax = max;
  }

  void calculateDatumSize() {
    int level = 0;
    while (true) {
      if ((deltaMax >> level) != 0) {
        level++;
      } else {
        break;
      }
    }
    if (level == 0) level = 1;

    int head = level / 8;
    int tail = level % 8;
    if (tail)
      datumLength = head + 1;
    else
      datumLength = head;
    datumBit = level;
  }

  void checkDistribution() {
    //      BitDistribution bitDistributionTotal;
    //      std::vector<BitDistribution> tempList;
    //      for(int i=0; i<length; i++) {
    //        BitDistribution bitDistribution = getBitDistribution2(output[i] ,
    //        datumBit); tempList.push_back(bitDistribution);
    //        bitDistributionTotal = bitDistributionTotal + bitDistribution;
    ////        maskOutput.push_back(output[i] ^ mask);
    //      }
    //      ratio = bitDistributionTotal.ratio;
    ratio = ratioList[datumBit - 1];

    mode = ::getMode(output, datumBit);
  }

  std::string athEncodeWithoutMask() {
    std::string result;

    if (datumBit <= 8) {
      for (int i = 0; i < length; i++) {
        result.append((char*)&output[i], datumLength);
      }
      newLength += result.size();
      return result;
    }

    std::string athStr;
    for (int i = 0; i < length; i++) {
      EncodeOutput encodeOutput = AthEncoding(output[i], datumBit, mode);
      athStr.append((char*)encodeOutput.output, encodeOutput.length);
    }

    size_t numLength = athStr.size();
    if (numLength > datumLength * length) {
      for (int i = 0; i < length; i++) {
        result.append((char*)&output[i], datumLength);
      }
      newLength += result.size();
    } else {
      result.append(athStr);
      newLength += result.size();
      valid = true;
    }

    return result;
  }

  std::string athEncodeWithoutMask2() {
    std::string athStr;
    std::unordered_map<uint8_t, std::unordered_map<uint8_t, HeadNode>>&
        modeCheckTable = checkTableList[mode];
    std::unordered_map<uint8_t, HeadNode>& checkTable =
        modeCheckTable[datumBit];
    int testlength = checkTable.size();
    printf("%d\n", testlength);
    for (int i = 0; i < length; i++) {
      EncodeOutput encodeOutput = AthEncoding(output[i], datumBit, mode);
      HeadNode& headNode = checkTable[encodeOutput.output[0]];
      if (!headNode.end) {
        State& state = headNode.table[encodeOutput.output[1]];
        if (state.end && state.length > 2) {
          athStr.append((char*)encodeOutput.output, 2);
          continue;
        }
      }
      athStr.append((char*)encodeOutput.output, encodeOutput.length);
    }

    std::string result;
    result.append(athStr);
    newLength += result.size();
    valid = true;

    return result;
  }

  std::string naiveEncode() {
    std::string result;
    result.append((char*)&length, sizeof(length));
    result.append((char*)&deltaMin, sizeof(deltaMin));
    for (int i = 0; i < length; i++) {
      result.append((char*)&output[i], datumLength);
    }
    oldLength += result.size();

    return result;
  }

  std::vector<int64_t> dataList;
  std::vector<uint64_t> output;
  int64_t deltaMin{};
  int64_t deltaMax;
  int datumLength;
  int datumBit;
  int ratio;
  uint32_t length = 0;
  bool valid = false;
  uint8_t mode = 0;

  uint64_t oldLength = 0, newLength = 0;
};

#endif  // SIMBASEDREORDER_ATHCOMPRESSOR_H
