//
// Created by Borelset on 2023/7/13.
//

#ifndef SIMBASEDREORDER_ATHENCODING_H
#define SIMBASEDREORDER_ATHENCODING_H

static int ratioList[] = {
    0,    0,    0,    0,    0,    0,    0,    0,
    1218,  // ratio   1218/4096
    972,   // 10 -> 8  972/4096
    809,   // 11 -> 8
    686,  604,  522,  481,
    440,   // 16->8 440/4096
    1423,  // 17->16
    1218, 1095, 972,  890,  809,  727,  686,  1546, 1341, 1218, 1136, 1054,
    972,  890,  849,  1587, 1423, 1300, 1218, 1136, 1054, 1013, 972,  1628,
    1505, 1382, 1300, 1218, 1177, 1095, 1054, 1668, 1546, 1423, 1341, 1300,
    1218, 1177, 1136, 1709, 1587, 1464, 1382, 1341, 1259, 1218, 1177,
};

uint32_t ByteMask[4] = {
    0xff000000,
    0x00ff0000,
    0x0000ff00,
    0x000000ff,
};

uint8_t getByte(uint32_t value, int pos) {
  value = value & ByteMask[pos];
  return value >> (8 * (3 - pos));
}

struct EncodeOutput {
  uint8_t output[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int length = 0;

  void write(uint8_t value) {
    output[length] = value;
    length++;
  }
};

struct PrebuildState {
  uint32_t x1 = 0, x2 = 0;
  int ratio = 0;
  uint8_t output[4] = {0, 0, 0, 0};
  int length = 0;
  int bitPos = 0;
  int usedLength = 0;
};

struct DecodeOutput {
  uint8_t output[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int length = 0;
  int bitPos = 0;
  int usedLength = 0;

  void write(int bit) {
    output[length] = output[length] | (bit << bitPos);
    bitPos++;
    if (bitPos == 8) {
      length++;
      bitPos = 0;
    }
  }

  void flush() {
    if (bitPos != 0) length++;
  }
};

struct Ratio {
  uint32_t ratio = 0;
  int count = 0;

  Ratio(int initRatio) : ratio{static_cast<uint32_t>(initRatio << 10)} {}

  void update(int bit) {
    int delta = (((bit << 22) - ratio) * 2);
    int update = delta / (count + count + 20);
    assert(update + ratio >= 0);
    ratio = update + ratio;
    ratio = ratio & 0x003fffff;  // 8 + 8 + 6 = 22
    count++;
  }

  int getRatio() { return ratio >> 10; }
};

uint8_t getMode(std::vector<uint64_t>& data, int datumBit) {
  std::vector<int> records(datumBit);
  for (uint64_t value : data) {
    for (int i = 0; i < datumBit; i++) {
      int bit = value & 0x01;
      if (bit) {
        records[i]++;
      }
      value = value >> 1;
    }
  }
  int segSize = datumBit / 3;
  uint8_t mode = 0;

  int sum = 0, total = 0;
  for (int i = 0; i < segSize; i++) {
    sum += records[i];
    total += data.size();
  }
  mode = mode << 1;
  if (sum * 2 > total) {
    mode = mode + 1;
  }

  sum = 0, total = 0;
  for (int i = segSize; i < 2 * segSize; i++) {
    sum += records[i];
    total += data.size();
  }
  mode = mode << 1;
  if (sum * 2 > total) {
    mode = mode + 1;
  }

  sum = 0, total = 0;
  for (int i = 2 * segSize; i < datumBit; i++) {
    sum += records[i];
    total += data.size();
  }
  mode = mode << 1;
  if (sum * 2 > total) {
    mode = mode + 1;
  }

  return mode;
}

EncodeOutput AthEncoding(uint64_t value, int datumBit, int mode) {
  EncodeOutput result;
  uint32_t x1 = 0, x2 = UINT32_MAX;
  int segSize = datumBit / 3;

  {  // head segment
    int segMode = mode & 0x01;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = 0; i < segSize; i++) {
      int bit = (value & 0x01);
      value = value >> 1;
      uint32_t xMid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >>
                       12);  // x1 + (x2-x1)*ratio //下一个分界点在哪
      bit ? (x2 = xMid) : (x1 = xMid + 1);
      pr.update(bit);  // 经验性
      while (((x1 ^ x2) & 0xff000000) ==
             0) {  // uint32_t 值域 0~2^32-1  输出前缀
        result.write(x1 >> 24);
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
      }
    }
  }
  {  // mid segment
    int segMode = mode & 0x02;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = segSize; i < 2 * segSize; i++) {
      int bit = (value & 0x01);
      value = value >> 1;
      uint32_t xMid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit ? (x2 = xMid) : (x1 = xMid + 1);
      pr.update(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        result.write(x1 >> 24);
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
      }
    }
  }
  {  // tail segment
    int segMode = mode & 0x04;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1]; 
    Ratio pr(refRatio);
    for (int i = 2 * segSize; i < datumBit; i++) {
      int bit = (value & 0x01);
      value = value >> 1;
      uint32_t xMid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit ? (x2 = xMid) : (x1 = xMid + 1);
      pr.update(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        result.write(x1 >> 24);
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
      }
    }
  }
  for (int i = 0; i < 4; i++) {  // 判断输出是否足够  x1 = 0xff 0xfe 0x00 0x00
                                 // x2 = 0xfc 0xfc 0x00 0x00
    uint32_t head1 = x1 >> (8 * (3 - i));
    uint32_t head2 = x2 >> (8 * (3 - i));
    if (head2 - head1 > 1) {
      for (int j = 3 - i; j < 4; j++) {
        uint32_t finalValue = head1 + 1;
        result.write(getByte(finalValue, j));
      }
      break;
    }
  }
  return result;
}

DecodeOutput AthDecoding(uint8_t* output, int datumBit, int mode) {
  uint32_t x1 = 0, x2 = UINT32_MAX;
  uint32_t x = 0;
  int pos = 0, usedLength = 0;
  for (int i = 0; i < 4; i++) {
    x = (x << 8) + output[i];
    pos++;
  }

  DecodeOutput result;
  int segSize = datumBit / 3;
  {  // head segment
    int segMode = mode & 0x01;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = 0; i < segSize; i++) {
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
      }
      result.write(bit);
    }
  }
  {  // mid segment
    int segMode = mode & 0x02;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = segSize; i < 2 * segSize; i++) {
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
      }
      result.write(bit);
    }
  }
  {  // tail segment
    int segMode = mode & 0x04;
    int refRatio = segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = 2 * segSize; i < datumBit; i++) {
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
      }
      result.write(bit);
    }
  }
  for (int i = 0; i < 4; i++) {
    uint32_t head1 = x1 >> (8 * (3 - i));
    uint32_t head2 = x2 >> (8 * (3 - i));
    if (head2 - head1 > 1) {
      usedLength++;
      break;
    } else {
      usedLength++;
    }
  }

  result.usedLength = usedLength;
  result.flush();

  return result;
}

DecodeOutput AthDecoding_fast(uint8_t* output, int datumBit,
                              PrebuildState prebuildState, int mode) {
  uint32_t x1 = prebuildState.x1, x2 = prebuildState.x2;
  uint32_t x = 0;
  int pos = 0, usedLength = prebuildState.usedLength;
  for (int i = 0; i < 4; i++) {
    x = (x << 8) + output[i];
    pos++;
  }

  DecodeOutput result;
  result.bitPos = prebuildState.bitPos;
  result.length = prebuildState.length;
  for (int j = 0; j <= result.length; j++) {
    result.output[j] = prebuildState.output[j];
  }

  int producedBits = result.length * 8 + result.bitPos;
  int segSize = datumBit / 3;

  if (producedBits < segSize) {
    {
      int segMode = mode & 0x01;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      pr.ratio = prebuildState.ratio;
      pr.count = producedBits;
      for (int i = producedBits; i < segSize; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
    {
      int segMode = mode & 0x02;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      for (int i = segSize; i < 2 * segSize; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
    {
      int segMode = mode & 0x04;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      for (int i = 2 * segSize; i < datumBit; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
  } else if (producedBits < 2 * segSize) {
    {
      int segMode = mode & 0x02;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      if (producedBits != segSize) {
        pr.ratio = prebuildState.ratio;
        pr.count = producedBits - segSize;
      }
      for (int i = producedBits; i < 2 * segSize; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
    {
      int segMode = mode & 0x04;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      for (int i = 2 * segSize; i < datumBit; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
  } else if (producedBits < datumBit) {
    {
      int segMode = mode & 0x04;
      int refRatio =
          segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
      Ratio pr(refRatio);
      if (producedBits != 2 * segSize) {
        pr.ratio = prebuildState.ratio;
        pr.count = producedBits - 2 * segSize;
      }
      for (int i = producedBits; i < datumBit; i++) {
        int bit;
        uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                        (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
        bit = (x <= xmid);
        bit ? (x2 = xmid) : (x1 = xmid + 1);
        pr.update(bit);
        while (((x1 ^ x2) & 0xff000000) == 0) {
          x1 <<= 8;
          x2 = (x2 << 8) + 255;
          x = (x << 8) + (output[pos++]);
          usedLength++;
        }
        result.write(bit);
      }
    }
  } else {
    assert(0);
  }

  for (int i = 0; i < 4; i++) {
    uint32_t head1 = x1 >> (8 * (3 - i));
    uint32_t head2 = x2 >> (8 * (3 - i));
    if (head2 - head1 > 1) {
      usedLength++;
      break;
    } else {
      usedLength++;
    }
  }

  result.usedLength = usedLength;
  result.flush();

  return result;
}

PrebuildState AthDecoding_prebuild(uint8_t* output, int datumBit,
                                   int outputLimit, int inputLimit,
                                   uint8_t mode) {
  uint32_t x1 = 0, x2 = UINT32_MAX;
  uint32_t x = 0;
  int pos = 0, usedLength = 0;
  for (int i = 0; i < 4; i++) {
    x = (x << 8) + output[i];
    pos++;
  }

  DecodeOutput result;
  PrebuildState prebuildState;
  int segSize = datumBit / 3;

  {
    int segMode = mode & 0x01;
    int refRatio =
        segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = 0; i < segSize; i++) {
      if (i == inputLimit) {
        prebuildState.x1 = x1;
        prebuildState.x2 = x2;
        prebuildState.ratio = pr.ratio;
        prebuildState.bitPos = result.bitPos;
        prebuildState.length = result.length;
        for (int j = 0; j <= result.length; j++) {
          prebuildState.output[j] = result.output[j];
        }
        prebuildState.usedLength = usedLength;
        return prebuildState;
      }
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      result.write(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
        if (usedLength == outputLimit) {
          prebuildState.x1 = x1;
          prebuildState.x2 = x2;
          prebuildState.ratio = pr.ratio;
          prebuildState.bitPos = result.bitPos;
          prebuildState.length = result.length;
          for (int j = 0; j <= result.length; j++) {
            prebuildState.output[j] = result.output[j];
          }
          prebuildState.usedLength = usedLength;
          return prebuildState;
        }
      }
    }
  }
  {
    int segMode = mode & 0x02;
    int refRatio =
        segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = segSize; i < 2 * segSize; i++) {
      if (i == inputLimit) {
        prebuildState.x1 = x1;
        prebuildState.x2 = x2;
        prebuildState.ratio = pr.ratio;
        prebuildState.bitPos = result.bitPos;
        prebuildState.length = result.length;
        for (int j = 0; j <= result.length; j++) {
          prebuildState.output[j] = result.output[j];
        }
        prebuildState.usedLength = usedLength;
        return prebuildState;
      }
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      result.write(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
        if (usedLength == outputLimit) {
          prebuildState.x1 = x1;
          prebuildState.x2 = x2;
          prebuildState.ratio = pr.ratio;
          prebuildState.bitPos = result.bitPos;
          prebuildState.length = result.length;
          for (int j = 0; j <= result.length; j++) {
            prebuildState.output[j] = result.output[j];
          }
          prebuildState.usedLength = usedLength;
          return prebuildState;
        }
      }
    }
  }
  {
    int segMode = mode & 0x04;
    int refRatio =
        segMode ? 4095 - ratioList[datumBit - 1] : ratioList[datumBit - 1];
    Ratio pr(refRatio);
    for (int i = 2 * segSize; i < datumBit; i++) {
      if (i == inputLimit) {
        prebuildState.x1 = x1;
        prebuildState.x2 = x2;
        prebuildState.ratio = pr.ratio;
        prebuildState.bitPos = result.bitPos;
        prebuildState.length = result.length;
        for (int j = 0; j <= result.length; j++) {
          prebuildState.output[j] = result.output[j];
        }
        prebuildState.usedLength = usedLength;
        return prebuildState;
      }
      int bit;
      uint32_t xmid = x1 + ((x2 - x1) >> 12) * pr.getRatio() +
                      (((x2 - x1) & 0xfff) * pr.getRatio() >> 12);
      bit = (x <= xmid);
      bit ? (x2 = xmid) : (x1 = xmid + 1);
      pr.update(bit);
      result.write(bit);
      while (((x1 ^ x2) & 0xff000000) == 0) {
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        x = (x << 8) + (output[pos++]);
        usedLength++;
        if (usedLength == outputLimit) {
          prebuildState.x1 = x1;
          prebuildState.x2 = x2;
          prebuildState.ratio = pr.ratio;
          prebuildState.bitPos = result.bitPos;
          prebuildState.length = result.length;
          for (int j = 0; j <= result.length; j++) {
            prebuildState.output[j] = result.output[j];
          }
          prebuildState.usedLength = usedLength;
          return prebuildState;
        }
      }
    }
  }

  assert(0);

  return prebuildState;
}

#endif  // SIMBASEDREORDER_ATHENCODING_H
