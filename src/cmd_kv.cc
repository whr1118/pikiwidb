/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "cmd_kv.h"
#include "common.h"
#include "pstd_string.h"
#include "pstd_util.h"
#include "store.h"

namespace pikiwidb {

GetCmd::GetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool GetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err != kPErrorOK) {
    if (err == kPErrorNotExist) {
      client->AppendString("");
    } else {
      client->SetRes(CmdRes::kSyntaxErr, "get key error");
    }
    return;
  }
  auto str = GetDecodedString(value);
  std::string reply(str->c_str(), str->size());
  client->AppendString(reply);
}

SetCmd::SetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SetCmd::DoCmd(PClient* client) {
  PSTORE.ClearExpire(client->argv_[1]);  // clear key's old ttl
  PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[2]));
  client->SetRes(CmdRes::kOK);
}

AppendCmd::AppendCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool AppendCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void AppendCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err != kPErrorOK) {
    if (err == kPErrorNotExist) {            // = set command
      PSTORE.ClearExpire(client->argv_[1]);  // clear key's old ttl
      PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[2]));
      client->AppendInteger(static_cast<int64_t>(client->argv_[2].size()));
    } else {
      client->SetRes(CmdRes::kErrOther, "append cmd error");
    }
    return;
  }
  auto str = GetDecodedString(value);
  std::string old_value(str->c_str(), str->size());
  std::string new_value = old_value + client->argv_[2];
  PSTORE.SetValue(client->argv_[1], PObject::CreateString(new_value));
  client->AppendInteger(static_cast<int64_t>(new_value.size()));
}

GetSetCmd::GetSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool GetSetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetSetCmd::DoCmd(PClient* client) {
  PObject* old_value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), old_value, kPTypeString);
  if (err != kPErrorOK) {
    if (err == kPErrorNotExist) {            // = set command
      PSTORE.ClearExpire(client->argv_[1]);  // clear key's old ttl
      PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[2]));
      client->AppendString("");
    } else {
      client->SetRes(CmdRes::kErrOther, "getset cmd error");
    }
    return;
  }
  auto str = GetDecodedString(old_value);
  PSTORE.ClearExpire(client->argv_[1]);  // clear key's old ttl
  PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[2]));
  client->AppendString(*str);
}

MGetCmd::MGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool MGetCmd::DoInitial(PClient* client) {
  std::vector<std::string> keys(client->argv_.begin(), client->argv_.end());
  keys.erase(keys.begin());
  client->SetKey(keys);
  return true;
}

void MGetCmd::DoCmd(PClient* client) {
  size_t valueSize = client->Keys().size();
  client->AppendArrayLen(static_cast<int64_t>(valueSize));
  for (const auto& k : client->Keys()) {
    PObject* value = nullptr;
    PError err = PSTORE.GetValueByType(k, value, kPTypeString);
    if (err == kPErrorNotExist) {
      client->AppendStringLen(-1);
    } else {
      auto str = GetDecodedString(value);
      std::string reply(str->c_str(), str->size());
      client->AppendStringLen(static_cast<int64_t>(reply.size()));
      client->AppendContent(reply);
    }
  }
}

MSetCmd::MSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool MSetCmd::DoInitial(PClient* client) {
  size_t argcSize = client->argv_.size();
  if (argcSize % 2 == 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameMSet);
    return false;
  }
  std::vector<std::string> keys;
  for (size_t index = 1; index < argcSize; index += 2) {
    keys.emplace_back(client->argv_[index]);
  }
  client->SetKey(keys);
  return true;
}

void MSetCmd::DoCmd(PClient* client) {
  int valueIndex = 2;
  for (const auto& it : client->Keys()) {
    PSTORE.ClearExpire(it);  // clear key's old ttl
    PSTORE.SetValue(it, PObject::CreateString(client->argv_[valueIndex]));
    valueIndex += 2;
  }
  client->SetRes(CmdRes::kOK);
}

BitCountCmd::BitCountCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool BitCountCmd::DoInitial(PClient* client) {
  size_t paramSize = client->argv_.size();
  if (paramSize != 2 && paramSize != 4) {
    client->SetRes(CmdRes::kSyntaxErr, kCmdNameBitCount);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void BitCountCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->argv_[1], value, kPTypeString);
  if (err != kPErrorOK) {
    if (err == kPErrorNotExist) {
      client->AppendInteger(0);
    } else {
      client->SetRes(CmdRes::kErrOther, "bitcount get key error");
    }
    return;
  }

  int64_t start_offset_;
  int64_t end_offset_;
  if (pstd::String2int(client->argv_[2], &start_offset_) == 0 ||
      pstd::String2int(client->argv_[3], &end_offset_) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  auto str = GetDecodedString(value);
  auto value_length = static_cast<int64_t>(str->size());
  if (start_offset_ < 0) {
    start_offset_ += value_length;
  }
  if (end_offset_ < 0) {
    end_offset_ += value_length;
  }
  if (start_offset_ < 0) {
    start_offset_ = 0;
  }
  if (end_offset_ < 0) {
    end_offset_ = 0;
  }
  if (end_offset_ >= value_length) {
    end_offset_ = value_length - 1;
  }
  size_t count = 0;
  if (end_offset_ >= start_offset_) {
    count = BitCount(reinterpret_cast<const uint8_t*>(str->data()) + start_offset_, end_offset_ - start_offset_ + 1);
  }
  client->AppendInteger(static_cast<int64_t>(count));
}

DecrCmd::DecrCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool DecrCmd::DoInitial(pikiwidb::PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void DecrCmd::DoCmd(pikiwidb::PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err == kPErrorNotExist) {
    value = PSTORE.SetValue(client->Key(), PObject::CreateString(-1));
    client->AppendInteger(-1);
    return;
  }

  if (err != kPErrorOK) {
    client->SetRes(CmdRes::kErrOther);
    return;
  }

  if (value->encoding != kPEncodeInt) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  intptr_t oldVal = static_cast<intptr_t>(reinterpret_cast<std::intptr_t>(value->value));
  value->Reset(reinterpret_cast<void*>(oldVal - 1));

  client->AppendInteger(oldVal - 1);
}

IncrCmd::IncrCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool IncrCmd::DoInitial(pikiwidb::PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void IncrCmd::DoCmd(pikiwidb::PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err == kPErrorNotExist) {
    value = PSTORE.SetValue(client->Key(), PObject::CreateString(1));
    client->AppendInteger(1);
    return;
  }

  if (err != kPErrorOK) {
    client->SetRes(CmdRes::kErrOther);
    return;
  }

  if (value->encoding != kPEncodeInt) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  intptr_t oldVal = static_cast<intptr_t>(reinterpret_cast<std::intptr_t>(value->value));
  value->Reset(reinterpret_cast<void*>(oldVal + 1));

  client->AppendInteger(oldVal + 1);
}

BitOpCmd::BitOpCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool BitOpCmd::DoInitial(PClient* client) {
  if (!(pstd::StringEqualCaseInsensitive(client->argv_[1], "and") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "or") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "not") ||
        pstd::StringEqualCaseInsensitive(client->argv_[1], "xor"))) {
    client->SetRes(CmdRes::kSyntaxErr, "operation error");
    return false;
  }
  return true;
}

static std::string StringBitOp(const std::vector<std::string>& keys, BitOpCmd::BitOp op) {
  PString res;

  switch (op) {
    case BitOpCmd::kBitOpAnd:
    case BitOpCmd::kBitOpOr:
    case BitOpCmd::kBitOpXor:
      for (auto k : keys) {
        PObject* val = nullptr;
        if (PSTORE.GetValueByType(k, val, kPTypeString) != kPErrorOK) {
          continue;
        }

        auto str = GetDecodedString(val);
        if (res.empty()) {
          res = *str;
          continue;
        }

        if (str->size() > res.size()) {
          res.resize(str->size());
        }

        for (size_t i = 0; i < str->size(); ++i) {
          if (op == BitOpCmd::kBitOpAnd) {
            res[i] &= (*str)[i];
          } else if (op == BitOpCmd::kBitOpOr) {
            res[i] |= (*str)[i];
          } else if (op == BitOpCmd::kBitOpXor) {
            res[i] ^= (*str)[i];
          }
        }
      }
      break;

    case BitOpCmd::kBitOpNot: {
      assert(keys.size() == 1);
      PObject* val = nullptr;
      if (PSTORE.GetValueByType(keys[0], val, kPTypeString) != kPErrorOK) {
        break;
      }

      auto str = GetDecodedString(val);
      res.resize(str->size());

      for (size_t i = 0; i < str->size(); ++i) {
        res[i] = ~(*str)[i];
      }

      break;
    }

    default:
      break;
  }

  return res;
}

void BitOpCmd::DoCmd(PClient* client) {
  std::vector<std::string> keys;
  for (size_t i = 3; i < client->argv_.size(); ++i) {
    keys.push_back(client->argv_[i]);
  }

  PError err = kPErrorParam;
  PString res;

  if (client->Key().size() == 2) {
    if (pstd::StringEqualCaseInsensitive(client->argv_[1], "or")) {
      err = kPErrorOK;
      res = StringBitOp(keys, kBitOpOr);
    }
  } else if (client->Key().size() == 3) {
    if (pstd::StringEqualCaseInsensitive(client->argv_[1], "xor")) {
      err = kPErrorOK;
      res = StringBitOp(keys, kBitOpXor);
    } else if (pstd::StringEqualCaseInsensitive(client->argv_[1], "and")) {
      err = kPErrorOK;
      res = StringBitOp(keys, kBitOpAnd);
    } else if (pstd::StringEqualCaseInsensitive(client->argv_[1], "not")) {
      if (client->argv_.size() == 4) {
        err = kPErrorOK;
        res = StringBitOp(keys, kBitOpNot);
      }
    }
  }

  if (err != kPErrorOK) {
    client->SetRes(CmdRes::kSyntaxErr);
  } else {
    PSTORE.SetValue(client->argv_[2], PObject::CreateString(res));
    client->SetRes(CmdRes::kOK, std::to_string(static_cast<long>(res.size())));
  }
  client->SetRes(CmdRes::kOK, std::to_string(static_cast<long>(res.size())));
}

StrlenCmd::StrlenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool StrlenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void StrlenCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);

  switch (err) {
    case kPErrorOK: {
      auto str = GetDecodedString(value);
      size_t len = str->size();
      client->AppendInteger(static_cast<int64_t>(len));
      break;
    }
    case kPErrorNotExist: {
      client->AppendInteger(0);
      break;
    }
    default: {
      client->SetRes(CmdRes::kErrOther, "error other");
      break;
    }
  }
}

SetExCmd::SetExCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetExCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  int64_t sec = 0;
  if (pstd::String2int(client->argv_[2], &sec) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  return true;
}

void SetExCmd::DoCmd(PClient* client) {
  PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[3]));
  int64_t sec = 0;
  pstd::String2int(client->argv_[2], &sec);
  PSTORE.SetExpire(client->argv_[1], pstd::UnixMilliTimestamp() + sec * 1000);
  client->SetRes(CmdRes::kOK);
}

PSetExCmd::PSetExCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool PSetExCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  int64_t msec = 0;
  if (pstd::String2int(client->argv_[2], &msec) == 0) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  return true;
}

void PSetExCmd::DoCmd(PClient* client) {
  PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[3]));
  int64_t msec = 0;
  pstd::String2int(client->argv_[2], &msec);
  PSTORE.SetExpire(client->argv_[1], pstd::UnixMilliTimestamp() + msec);
  client->SetRes(CmdRes::kOK);
}

IncrbyCmd::IncrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool IncrbyCmd::DoInitial(PClient* client) {
  int64_t by_ = 0;
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by_))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void IncrbyCmd::DoCmd(PClient* client) {
  int64_t new_value = 0;
  int64_t by_ = 0;
  pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by_);
  PError err = PSTORE.Incrby(client->Key(), by_, &new_value);
  switch (err) {
    case kPErrorType:
      client->SetRes(CmdRes::kInvalidInt);
      break;
    case kPErrorNotExist:                 // key not exist, set a new value
      PSTORE.ClearExpire(client->Key());  // clear key's old ttl
      PSTORE.SetValue(client->Key(), PObject::CreateString(by_));
      client->AppendInteger(by_);
      break;
    case kPErrorOK:
      client->AppendInteger(new_value);
      break;
    default:
      client->SetRes(CmdRes::kErrOther, "incrby cmd error");
      break;
  }
}

DecrbyCmd::DecrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool DecrbyCmd::DoInitial(PClient* client) {
  int64_t by = 0;
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void DecrbyCmd::DoCmd(PClient* client) {
  int64_t new_value = 0;
  int64_t by = 0;
  pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &by);
  PError err = PSTORE.Decrby(client->Key(), by, &new_value);
  switch (err) {
    case kPErrorType:
      client->SetRes(CmdRes::kInvalidInt);
      break;
    case kPErrorNotExist:  // key not exist, set a new value
      by *= -1;
      PSTORE.ClearExpire(client->Key());  // clear key's old ttl
      PSTORE.SetValue(client->Key(), PObject::CreateString(by));
      client->AppendInteger(by);
      break;
    case kPErrorOK:
      client->AppendInteger(new_value);
      break;
    default:
      client->SetRes(CmdRes::kErrOther, "decrby cmd error");
      break;
  }
}

IncrbyFloatCmd::IncrbyFloatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool IncrbyFloatCmd::DoInitial(PClient* client) {
  long double by_ = 0.00f;
  if (StrToLongDouble(client->argv_[2].data(), client->argv_[2].size(), &by_)) {
    client->SetRes(CmdRes::kInvalidFloat);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void IncrbyFloatCmd::DoCmd(PClient* client) {
  std::string new_value;
  PError err = PSTORE.Incrbyfloat(client->argv_[1], client->argv_[2], &new_value);
  switch (err) {
    case kPErrorType:
      client->SetRes(CmdRes::kInvalidFloat);
      break;
    case kPErrorNotExist:                 // key not exist, set a new value
      PSTORE.ClearExpire(client->Key());  // clear key's old ttl
      PSTORE.SetValue(client->Key(), PObject::CreateString(client->argv_[2]));
      client->AppendString(client->argv_[2]);
      break;
    case kPErrorOK:
      client->AppendString(new_value);
      break;
    default:
      client->SetRes(CmdRes::kErrOther, "incrbyfloat cmd error");
      break;
  }
}

SetNXCmd::SetNXCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetNXCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SetNXCmd::DoCmd(PClient* client) {
  int iSuccess = 1;
  PObject* value = nullptr;
  PError err = PSTORE.GetValue(client->argv_[1], value);
  if (err == kPErrorNotExist) {
    PSTORE.ClearExpire(client->argv_[1]);  // clear key's old ttl
    PSTORE.SetValue(client->argv_[1], PObject::CreateString(client->argv_[2]));
    client->AppendInteger(iSuccess);
  } else {
    client->AppendInteger(!iSuccess);
  }
}

GetBitCmd::GetBitCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool GetBitCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void GetBitCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err != kPErrorOK) {
    client->SetRes(CmdRes::kErrOther);
    return;
  }

  long offset = 0;
  if (!Strtol(client->argv_[2].c_str(), client->argv_[2].size(), &offset)) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  auto str = GetDecodedString(value);
  const uint8_t* buf = (const uint8_t*)str->c_str();
  size_t size = 8 * str->size();

  if (offset < 0 || offset >= static_cast<long>(size)) {
    client->AppendInteger(0);
    return;
  }

  size_t bytesOffset = offset / 8;
  size_t bitsOffset = offset % 8;
  uint8_t byte = buf[bytesOffset];
  if (byte & (0x1 << bitsOffset)) {
    client->AppendInteger(1);
  } else {
    client->AppendInteger(0);
  }

  return;
}

GetRangeCmd::GetRangeCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryString) {}

bool GetRangeCmd::DoInitial(PClient* client) {
  // > range key start end
  int64_t start = 0;
  int64_t end = 0;
  // ERR value is not an integer or out of range
  if (!(pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &start)) ||
      !(pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &end))) {
    client->SetRes(CmdRes::kInvalidInt);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void GetRangeCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err != kPErrorOK) {
    if (err == kPErrorNotExist) {
      client->AppendString("");
    } else {
      client->SetRes(CmdRes::kErrOther, "getrange cmd error");
    }
    return;
  }

  int64_t start = 0;
  int64_t end = 0;
  pstd::String2int(client->argv_[2].data(), client->argv_[2].size(), &start);
  pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &end);

  auto str = GetDecodedString(value);
  size_t len = str->size();

  // if the start offset is greater than the end offset, return an empty string
  if (end < start) {
    client->AppendString("");
    return;
  }

  // calculate the offset
  // if it is a negative number, start from the end
  if (start < 0) {
    start += len;
  }
  if (end < 0) {
    end += len;
  }
  if (start < 0) {
    start = 0;
  }
  if (end < 0) {
    end = 0;
  }
  // if the offset exceeds the length of the string, set it to the end of the string.
  if (end >= len) {
    end = len - 1;
  }

  client->AppendString(str->substr(start, end - start + 1));
}

SetBitCmd::SetBitCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryString) {}

bool SetBitCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void SetBitCmd::DoCmd(PClient* client) {
  PObject* value = nullptr;
  PError err = PSTORE.GetValueByType(client->Key(), value, kPTypeString);
  if (err == kPErrorNotExist) {
    value = PSTORE.SetValue(client->Key(), PObject::CreateString(""));
    err = kPErrorOK;
  }

  if (err != kPErrorOK) {
    client->AppendInteger(0);
    return;
  }

  long offset = 0;
  long on = 0;
  if (!Strtol(client->argv_[2].c_str(), client->argv_[2].size(), &offset) ||
      !Strtol(client->argv_[3].c_str(), client->argv_[3].size(), &on)) {
    client->SetRes(CmdRes::kInvalidInt);
    return;
  }

  if (offset < 0 || offset > kStringMaxBytes) {
    client->AppendInteger(0);
    return;
  }

  PString* pStringPtr = value->CastString();
  if (!pStringPtr) {
    client->AppendInteger(0);
    return;
  }

  PString& newVal = *pStringPtr;

  size_t bytes = offset / 8;
  size_t bits = offset % 8;

  if (bytes + 1 > newVal.size()) {
    newVal.resize(bytes + 1, '\0');
  }

  const char oldByte = newVal[bytes];
  char& byte = newVal[bytes];
  if (on) {
    byte |= (0x1 << bits);
  } else {
    byte &= ~(0x1 << bits);
  }

  value->Reset(new PString(newVal));
  value->encoding = kPEncodeRaw;
  client->AppendInteger((oldByte & (0x1 << bits)) ? 1 : 0);
  return;
}

}  // namespace pikiwidb
