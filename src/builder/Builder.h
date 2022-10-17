/* Header for Builder class
   Copyright (C) 2018-2022 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "../common/Ctx.h"
#include "../common/LobCtx.h"
#include "../common/LobData.h"
#include "../common/LobKey.h"
#include "../common/RedoLogRecord.h"
#include "../common/RuntimeException.h"
#include "../common/types.h"
#include "../common/typeLobId.h"
#include "../common/typeTime.h"
#include "../common/typeXid.h"
#include "../locales/CharacterSet.h"
#include "../locales/Locales.h"

#ifndef BUILDER_H_
#define BUILDER_H_

#define OUTPUT_BUFFER_DATA_SIZE                 (MEMORY_CHUNK_SIZE - sizeof(struct BuilderQueue))
#define OUTPUT_BUFFER_ALLOCATED                 0x0001
#define OUTPUT_BUFFER_CONFIRMED                 0x0002
#define VALUE_BUFFER_MIN                        1048576
#define VALUE_BUFFER_MAX                        4294967296

namespace OpenLogReplicator {
    class Ctx;
    class CharacterSet;
    class Locales;
    class OracleTable;
    class Builder;
    class Metadata;
    class SystemTransaction;

    struct BuilderQueue {
        uint64_t id;
        std::atomic<uint64_t> length;
        uint8_t* data;
        std::atomic<BuilderQueue*> next;
    };

    struct BuilderMsg {
        void* ptr;
        uint64_t id;
        uint64_t queueId;
        uint64_t length;
        typeScn scn;
        typeSeq sequence;
        uint8_t* data;
        typeObj obj;
        uint16_t pos;
        uint16_t flags;
    };

    class Builder {
    protected:
        static const char map64[65];
        static const char map16[17];
        static const char map10[11];

        Ctx* ctx;
        Locales* locales;
        Metadata* metadata;
        BuilderMsg* msg;

        uint64_t messageFormat;
        uint64_t ridFormat;
        uint64_t xidFormat;
        uint64_t timestampFormat;
        uint64_t charFormat;
        uint64_t scnFormat;
        uint64_t unknownFormat;
        uint64_t schemaFormat;
        uint64_t columnFormat;
        uint64_t unknownType;
        uint64_t unconfirmedLength;
        uint64_t messageLength;
        uint64_t flushBuffer;
        char* valueBuffer;
        uint64_t valueBufferLength;
        uint64_t valueLength;
        std::unordered_set<OracleTable*> tables;
        typeTime lastTime;
        typeScn lastScn;
        typeSeq lastSequence;
        typeXid lastXid;
        uint64_t valuesSet[MAX_NO_COLUMNS / sizeof(uint64_t)];
        uint64_t valuesMerge[MAX_NO_COLUMNS / sizeof(uint64_t)];
        uint64_t lengths[MAX_NO_COLUMNS][4];
        uint8_t* values[MAX_NO_COLUMNS][4];
        uint64_t lengthsPart[3][MAX_NO_COLUMNS][4];
        uint8_t* valuesPart[3][MAX_NO_COLUMNS][4];
        uint64_t valuesMax;
        uint8_t* merges[MAX_NO_COLUMNS*4];
        uint64_t mergesMax;
        uint64_t id;
        uint64_t num;
        uint64_t maxMessageMb;      // Maximum message size able to handle by writer
        bool newTran;
        bool compressedBefore;
        bool compressedAfter;

        std::mutex mtx;
        std::condition_variable condNoWriterWork;

        float decodeFloat(const uint8_t* data);
        double decodeDouble(const uint8_t* data);
        void builderRotate(bool copy);
        void processValue(LobCtx* lobCtx, OracleTable* table, typeCol col, const uint8_t* data, uint64_t length, bool after, bool compressed);

        void valuesRelease() {
            for (uint64_t i = 0; i < mergesMax; ++i)
                delete[] merges[i];
            mergesMax = 0;

            uint64_t baseMax = valuesMax >> 6;
            for (uint64_t base = 0; base <= baseMax; ++base) {
                auto column = (typeCol)(base << 6);
                for (uint64_t mask = 1; mask != 0; mask <<= 1, ++column) {
                    if (valuesSet[base] < mask)
                        break;
                    if ((valuesSet[base] & mask) == 0)
                        continue;

                    valuesSet[base] &= ~mask;
                    values[column][VALUE_BEFORE] = nullptr;
                    values[column][VALUE_BEFORE_SUPP] = nullptr;
                    values[column][VALUE_AFTER] = nullptr;
                    values[column][VALUE_AFTER_SUPP] = nullptr;
                }
            }
            valuesMax = 0;
            compressedBefore = false;
            compressedAfter = false;
        };

        void valueSet(uint64_t type, uint16_t column, uint8_t* data, uint16_t length, uint8_t fb) {
            if ((ctx->trace2 & TRACE2_DML) != 0) {
                std::stringstream strStr;
                strStr << "DML: value: " << std::dec << type << "/" << column << "/" << std::dec << length << "/" << std::setfill('0') <<
                        std::setw(2) << std::hex << (uint64_t)fb << " to: ";
                for (uint64_t i = 0; i < length && i < 10; ++i) {
                    strStr << "0x" << std::setfill('0') << std::setw(2) << std::hex << (uint64_t)data[i] << ", ";
                }
                TRACE(TRACE2_DML, strStr.str())
            }

            uint64_t base = column >> 6;
            uint64_t mask = ((uint64_t)1) << (column & 0x3F);
            // New value
            if ((valuesSet[base] & mask) == 0) {
                valuesSet[base] |= mask;
                if (column >= valuesMax)
                    valuesMax = column + 1;
            }

            switch (fb & (FB_P | FB_N)) {
            case 0:
                lengths[column][type] = length;
                values[column][type] = data;
                break;

            case FB_N:
                lengthsPart[0][column][type] = length;
                valuesPart[0][column][type] = data;
                if ((valuesMerge[base] & mask) == 0)
                    valuesMerge[base] |= mask;
                break;

            case FB_P | FB_N:
                lengthsPart[1][column][type] = length;
                valuesPart[1][column][type] = data;
                if ((valuesMerge[base] & mask) == 0)
                    valuesMerge[base] |= mask;
                break;

            case FB_P:
                lengthsPart[2][column][type] = length;
                valuesPart[2][column][type] = data;
                if ((valuesMerge[base] & mask) == 0)
                    valuesMerge[base] |= mask;
                break;
            }
        };

        void builderShift(uint64_t bytes, bool copy) {
            lastBuffer->length += bytes;

            if (lastBuffer->length >= OUTPUT_BUFFER_DATA_SIZE)
                builderRotate(copy);
        };

        void builderShiftFast(uint64_t bytes) const {
            lastBuffer->length += bytes;
        };

        void builderBegin(typeObj obj) {
            messageLength = 0;

            if (lastBuffer->length + sizeof(struct BuilderMsg) >= OUTPUT_BUFFER_DATA_SIZE)
                builderRotate(true);

            msg = (BuilderMsg*)(lastBuffer->data + lastBuffer->length);
            builderShift(sizeof(struct BuilderMsg), true);
            msg->scn = lastScn;
            msg->sequence = lastSequence;
            msg->length = 0;
            msg->id = id++;
            msg->obj = obj;
            msg->pos = 0;
            msg->flags = 0;
            msg->data = lastBuffer->data + lastBuffer->length;
        };

        void builderCommit(bool force) {
            if (messageLength == 0) {
                WARNING("output buffer - commit of empty transaction")
            }

            msg->queueId = lastBuffer->id;
            builderShift((8 - (messageLength & 7)) & 7, false);
            unconfirmedLength += messageLength;
            msg->length = messageLength;

            if (force || flushBuffer == 0 || unconfirmedLength > flushBuffer) {
                {
                    std::unique_lock<std::mutex> lck(mtx);
                    condNoWriterWork.notify_all();
                }
                unconfirmedLength = 0;
            }
            msg = nullptr;
        };

        void builderAppend(char character) {
            lastBuffer->data[lastBuffer->length] = character;
            ++messageLength;
            builderShift(1, true);
        };

        void builderAppend(const char* str, uint64_t length) {
            if (lastBuffer->length + length < OUTPUT_BUFFER_DATA_SIZE) {
                memcpy((void*)(lastBuffer->data + lastBuffer->length), (void*)str, length);
                lastBuffer->length += length;
                messageLength += length;
            } else {
                for (uint64_t i = 0; i < length; ++i)
                    builderAppend(*str++);
            }
        };

        void builderAppend(const char* str) {
            char character = *str++;
            while (character != 0) {
                builderAppend(character);
                character = *str++;
            }
        };

        void builderAppend(std::string& str) {
            uint64_t length = str.length();
            if (lastBuffer->length + length < OUTPUT_BUFFER_DATA_SIZE) {
                memcpy(lastBuffer->data + lastBuffer->length, str.c_str(), length);
                lastBuffer->length += length;
                messageLength += length;
            } else {
                const char* charstr = str.c_str();
                for (uint64_t i = 0; i < length; ++i)
                    builderAppend(*charstr++);
            }
        };

        void columnUnknown(std::string& columnName, const uint8_t* data, uint64_t length) {
            valueBuffer[0] = '?';
            valueLength = 1;
            columnString(columnName);
            if (unknownFormat == UNKNOWN_FORMAT_DUMP) {
                std::stringstream ss;
                for (uint64_t j = 0; j < length; ++j)
                    ss << " " << std::hex << std::setfill('0') << std::setw(2) << (uint64_t) data[j];
                WARNING("unknown value (column: " << columnName << "): " << std::dec << length << " - " << ss.str())
            }
        };

        void valueBufferAppend(uint8_t value) {
            valueBuffer[valueLength++] = (char)value;
        };

        void valueBufferAppendHex(typeUnicode value, uint64_t length) {
            uint64_t j = (length - 1) * 4;
            valueBufferCheck(length);
            for (uint64_t i = 0; i < length; ++i) {
                valueBuffer[valueLength++] = map16[(value >> j) & 0xF];
                j -= 4;
            }
        };

        void parseNumber(const uint8_t* data, uint64_t length) {
            valueBufferPurge();
            valueBufferCheck(length * 2 + 2);

            uint8_t digits = data[0];
            // Just zero
            if (digits == 0x80) {
                valueBufferAppend('0');
            } else {
                uint64_t j = 1;
                uint64_t jMax = length - 1;

                // Positive number
                if (digits > 0x80 && jMax >= 1) {
                    uint64_t value;
                    uint64_t zeros = 0;
                    // Part of the total
                    if (digits <= 0xC0) {
                        valueBufferAppend('0');
                        zeros = 0xC0 - digits;
                    } else {
                        digits -= 0xC0;
                        // Part of the total - omitting first zero for first digit
                        value = data[j] - 1;
                        if (value < 10)
                            valueBufferAppend('0' + value);
                        else {
                            valueBufferAppend('0' + (value / 10));
                            valueBufferAppend('0' + (value % 10));
                        }

                        ++j;
                        --digits;

                        while (digits > 0) {
                            value = data[j] - 1;
                            if (j <= jMax) {
                                valueBufferAppend('0' + (value / 10));
                                valueBufferAppend('0' + (value % 10));
                                ++j;
                            } else {
                                valueBufferAppend('0');
                                valueBufferAppend('0');
                            }
                            --digits;
                        }
                    }

                    // Fraction part
                    if (j <= jMax) {
                        valueBufferAppend('.');

                        while (zeros > 0) {
                            valueBufferAppend('0');
                            valueBufferAppend('0');
                            --zeros;
                        }

                        while (j <= jMax - 1) {
                            value = data[j] - 1;
                            valueBufferAppend('0' + (value / 10));
                            valueBufferAppend('0' + (value % 10));
                            ++j;
                        }

                        // Last digit - omitting 0 at the end
                        value = data[j] - 1;
                        valueBufferAppend('0' + (value / 10));
                        if ((value % 10) != 0)
                            valueBufferAppend('0' + (value % 10));
                    }
                // Negative number
                } else if (digits < 0x80 && jMax >= 1) {
                    uint64_t value;
                    uint64_t zeros = 0;
                    valueBufferAppend('-');

                    if (data[jMax] == 0x66)
                        --jMax;

                    // Part of the total
                    if (digits >= 0x3F) {
                        valueBufferAppend('0');
                        zeros = digits - 0x3F;
                    } else {
                        digits = 0x3F - digits;

                        value = 101 - data[j];
                        if (value < 10)
                            valueBufferAppend('0' + value);
                        else {
                            valueBufferAppend('0' + (value / 10));
                            valueBufferAppend('0' + (value % 10));
                        }
                        ++j;
                        --digits;

                        while (digits > 0) {
                            if (j <= jMax) {
                                value = 101 - data[j];
                                valueBufferAppend('0' + (value / 10));
                                valueBufferAppend('0' + (value % 10));
                                ++j;
                            } else {
                                valueBufferAppend('0');
                                valueBufferAppend('0');
                            }
                            --digits;
                        }
                    }

                    if (j <= jMax) {
                        valueBufferAppend('.');

                        while (zeros > 0) {
                            valueBufferAppend('0');
                            valueBufferAppend('0');
                            --zeros;
                        }

                        while (j <= jMax - 1) {
                            value = 101 - data[j];
                            valueBufferAppend('0' + (value / 10));
                            valueBufferAppend('0' + (value % 10));
                            ++j;
                        }

                        value = 101 - data[j];
                        valueBufferAppend('0' + (value / 10));
                        if ((value % 10) != 0)
                            valueBufferAppend('0' + (value % 10));
                    }
                } else
                    throw RuntimeException("got unknown numeric value");
            }
        };

        std::string dumpLob(const uint8_t* data, uint64_t length) {
            std::stringstream ss;
            for (uint64_t j = 0; j < length; ++j) {
                ss << " " << std::setfill('0') << std::setw(2) << std::hex << (uint64_t)data[j];
            }
            return ss.str();
        }

        void parseLob(LobCtx* lobCtx, const uint8_t* data, uint64_t length, uint64_t charsetId, bool isClob) {
            valueLength = 0;

            if (length < 20) {
                WARNING("incorrect LOB data, length: " + std::to_string(length))
                return;
            }

            uint32_t flags = data[5];
            typeLobId lobId;
            lobId.set(data + 10);

            // details in index
            if ((flags & 0x04) == 0) {
                auto iLob = lobCtx->lobs.find(lobId);
                if (iLob == lobCtx->lobs.end()) {
                    WARNING("missing index for LOB: " + lobId.upper() << " data: " << dumpLob(data, length))
                    return;
                }
                LobData* lobData = iLob->second;
                valueBufferCheck((uint64_t) lobData->pageSize * (uint64_t) lobData->sizePages + lobData->sizeRest);

                bool append = false;
                uint32_t pageNo = 0;
                for (auto iLobPage: lobData->indexMap) {
                    uint32_t pageNoLob = iLobPage.first;
                    typeDba page = iLobPage.second;
                    if (pageNo != pageNoLob) {
                        WARNING("LOB: " << lobId.upper() << " incorrect page: " << pageNoLob << " while expected: "
                                         << pageNo << " data: " << dumpLob(data, length))
                        pageNo = pageNoLob;
                    }

                    auto iDataLob = lobData->dataMap.find(page);
                    if (iDataLob == lobData->dataMap.end()) {
                        WARNING("missing LOB data for LOB: " + lobId.upper() + " page: " + std::to_string(page))
                        return;
                    }
                    uint32_t chunkLength = lobData->pageSize;
                    if (pageNo == lobData->sizePages)
                        chunkLength = lobData->sizeRest;

                    valueBufferCheck(chunkLength * 4);
                    uint64_t redoLogRecordLength = *((uint64_t*) (iDataLob->second));
                    RedoLogRecord* redoLogRecordLob = (RedoLogRecord *) (iDataLob->second + sizeof(uint64_t));
                    redoLogRecordLob->data = (uint8_t*) (iDataLob->second + sizeof(uint64_t) + sizeof(RedoLogRecord));
                    if (isClob) {
                        parseString(redoLogRecordLob->data + redoLogRecordLob->lobData, chunkLength, charsetId, append);
                    } else {
                        memcpy(valueBuffer + valueLength, redoLogRecordLob->data + redoLogRecordLob->lobData, chunkLength);
                        valueLength += chunkLength;
                    };
                    append = true;
                    ++pageNo;
                }
            // details in value
            } else {
                uint16_t bodyLength = ctx->read16Big(data + 20);
                if (length != 20 + bodyLength) {
                    WARNING("incorrect LOB1, data: " << dumpLob(data, length))
                    return;
                }
                uint16_t flg2 = ctx->read16Big(data + 22);
                uint16_t sizeRest = ctx->read16Big(data + 24);
                uint8_t flg3 = data[26];

                uint16_t lobLength;
                uint64_t dataOffset;
                if ((flg3 & 0x0F) == 0) {
                    lobLength = data[28];
                    dataOffset = 30;
                } else if ((flg3 & 0x0F) == 1) {
                    lobLength = ctx->read16Big(data + 28);
                    dataOffset = 31;
                } else if ((flg3 & 0x0F) == 2) {
                    lobLength = ctx->read24Big(data + 28);
                    dataOffset = 32;
                } else if ((flg3 & 0x0F) == 3) {
                    lobLength = ctx->read32Big(data + 28);
                    dataOffset = 33;
                } else {
                    WARNING("incorrect LOB2, data: " << dumpLob(data, length))
                    return;
                }

                // in-row value
                if ((flg2 & 0x0800) == 0x0800) {
                    if (sizeRest != bodyLength - 6) {
                        WARNING("incorrect LOB3, data: " << dumpLob(data, length))
                        return;
                    }
                    parseString(data + dataOffset, lobLength, charsetId, false);
                } else {
                    bool append = false;
                    uint8_t lobPages = data[dataOffset++] + 1;

                    auto iLob = lobCtx->lobs.find(lobId);
                    if (iLob == lobCtx->lobs.end()) {
                        WARNING("missing index for LOB: " + lobId.upper() << " data: " << dumpLob(data, length))
                        return;
                    }
                    LobData* lobData = iLob->second;

                    for (uint64_t i = 0; i < lobPages; ++i) {
                        uint8_t flg4 = data[dataOffset++];
                        typeDba page = ctx->read32Big(data + dataOffset);
                        dataOffset += 4;

                        uint64_t pageCnt = 0;
                        if ((flg4 & 0xF0) == 0x00) {
                            pageCnt = data[dataOffset++];
                        } else if ((flg4 & 0xF0) == 0x20) {
                            pageCnt = ctx->read16Big(data + dataOffset);
                            dataOffset += 2;
                        } else {
                            WARNING("incorrect LOB4, data: " << dumpLob(data, length))
                            return;
                        }

                        for (uint64_t j = 0; j < pageCnt; ++j) {
                            auto iDataLob = lobData->dataMap.find(page);
                            if (iDataLob == lobData->dataMap.end()) {
                                WARNING("missing LOB data for LOB: " + lobId.upper() + " page: " + std::to_string(page))
                                return;
                            }
                            uint32_t chunkLength = lobData->pageSize;

                            valueBufferCheck(chunkLength * 4);
                            uint64_t redoLogRecordLength = *((uint64_t*) (iDataLob->second));
                            RedoLogRecord* redoLogRecordLob = (RedoLogRecord *) (iDataLob->second + sizeof(uint64_t));
                            redoLogRecordLob->data = (uint8_t*) (iDataLob->second + sizeof(uint64_t) + sizeof(RedoLogRecord));
                            chunkLength = redoLogRecordLob->lobDataLength;

                            if (isClob) {
                                parseString(redoLogRecordLob->data + redoLogRecordLob->lobData, chunkLength, charsetId, append);
                                append = true;
                            } else {
                                memcpy(valueBuffer + valueLength, redoLogRecordLob->data + redoLogRecordLob->lobData, chunkLength);
                                valueLength += chunkLength;
                            }
                            ++page;
                            lobLength -= chunkLength;
                        }
                    }

                    if (lobLength != 0) {
                        WARNING("incorrect LOB5, data: " << dumpLob(data, length) << ", left: " << std::dec << lobLength)
                    }
                }
            }
        }

        void parseString(const uint8_t* data, uint64_t length, uint64_t charsetId, bool append) {
            CharacterSet* characterSet = locales->characterMap[charsetId];
            if (characterSet == nullptr && (charFormat & CHAR_FORMAT_NOMAPPING) == 0)
                throw RuntimeException("can't find character set map for id = " + std::to_string(charsetId));
            if (!append)
                valueBufferPurge();

            while (length > 0) {
                typeUnicode unicodeCharacter;
                uint64_t unicodeCharacterLength;

                if ((charFormat & CHAR_FORMAT_NOMAPPING) == 0) {
                    unicodeCharacter = characterSet->decode(data, length);
                    unicodeCharacterLength = 8;
                } else {
                    unicodeCharacter = *data++;
                    --length;
                    unicodeCharacterLength = 2;
                }

                if ((charFormat & CHAR_FORMAT_HEX) != 0) {
                    valueBufferAppendHex(unicodeCharacter, unicodeCharacterLength);
                } else {
                    // 0xxxxxxx
                    if (unicodeCharacter <= 0x7F) {
                        valueBufferAppend(unicodeCharacter);

                    // 110xxxxx 10xxxxxx
                    } else if (unicodeCharacter <= 0x7FF) {
                        valueBufferAppend(0xC0 | (uint8_t)(unicodeCharacter >> 6));
                        valueBufferAppend(0x80 | (uint8_t)(unicodeCharacter & 0x3F));

                    // 1110xxxx 10xxxxxx 10xxxxxx
                    } else if (unicodeCharacter <= 0xFFFF) {
                        valueBufferAppend(0xE0 | (uint8_t)(unicodeCharacter >> 12));
                        valueBufferAppend(0x80 | (uint8_t)((unicodeCharacter >> 6) & 0x3F));
                        valueBufferAppend(0x80 | (uint8_t)(unicodeCharacter & 0x3F));

                    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                    } else if (unicodeCharacter <= 0x10FFFF) {
                        valueBufferAppend(0xF0 | (uint8_t)(unicodeCharacter >> 18));
                        valueBufferAppend(0x80 | (uint8_t)((unicodeCharacter >> 12) & 0x3F));
                        valueBufferAppend(0x80 | (uint8_t)((unicodeCharacter >> 6) & 0x3F));
                        valueBufferAppend(0x80 | (uint8_t)(unicodeCharacter & 0x3F));

                    } else
                        throw RuntimeException("got character code: U+" + std::to_string(unicodeCharacter));
                }
            }
        };

        void valueBufferCheck(uint64_t length) {
            if (valueLength + length > VALUE_BUFFER_MAX)
                throw RuntimeException("trying to allocate length for value: " + std::to_string(valueLength + length));

            if (valueLength + length < valueBufferLength)
                return;

            do {
                valueBufferLength <<= 1;
            } while (valueLength + length >= valueBufferLength);

            char* newValueBuffer = new char[valueBufferLength];
            memcpy(newValueBuffer, valueBuffer, valueLength);
            delete[] valueBuffer;
            valueBuffer = newValueBuffer;
        };

        void valueBufferPurge() {
            valueLength = 0;
            if (valueBufferLength == VALUE_BUFFER_MIN)
                return;

            delete[] valueBuffer;
            valueBuffer = new char[VALUE_BUFFER_MIN];
            valueBufferLength = VALUE_BUFFER_MIN;
        };

        virtual void columnFloat(std::string& columnName, float value) = 0;
        virtual void columnDouble(std::string& columnName, double value) = 0;
        virtual void columnString(std::string& columnName) = 0;
        virtual void columnNumber(std::string& columnName, uint64_t precision, uint64_t scale) = 0;
        virtual void columnRaw(std::string& columnName, const uint8_t* data, uint64_t length) = 0;
        virtual void columnTimestamp(std::string& columnName, struct tm &time_, uint64_t fraction, const char* tz) = 0;
        virtual void processInsert(LobCtx* lobCtx, OracleTable* table, typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, typeXid xid) = 0;
        virtual void processUpdate(LobCtx* lobCtx, OracleTable* table, typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, typeXid xid) = 0;
        virtual void processDelete(LobCtx* lobCtx, OracleTable* table, typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, typeXid xid) = 0;
        virtual void processDdl(OracleTable* table, typeDataObj dataObj, uint16_t type, uint16_t seq, const char* operation, const char* sql,
                                uint64_t sqlLength) = 0;
        virtual void processBeginMessage() = 0;

    public:
        SystemTransaction* systemTransaction;
        uint64_t buffersAllocated;
        BuilderQueue* firstBuffer;
        BuilderQueue* lastBuffer;

        Builder(Ctx* newCtx, Locales* newLocales, Metadata* newMetadata, uint64_t newMessageFormat, uint64_t newRidFormat, uint64_t newXidFormat,
                uint64_t newTimestampFormat, uint64_t newCharFormat, uint64_t newScnFormat, uint64_t newUnknownFormat, uint64_t newSchemaFormat,
                uint64_t newColumnFormat, uint64_t newUnknownType, uint64_t newFlushBuffer);
        virtual ~Builder();

        [[nodiscard]] uint64_t builderSize() const;
        [[nodiscard]] uint64_t getMaxMessageMb() const;
        void setMaxMessageMb(uint64_t maxMessageMb);
        void processBegin(typeScn scn, typeTime time_, typeSeq sequence, typeXid xid, bool system);
        void processInsertMultiple(LobCtx* lobCtx, RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2, bool system);
        void processDeleteMultiple(LobCtx* lobCtx, RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2, bool system);
        void processDml(LobCtx* lobCtx, RedoLogRecord* redoLogRecord1, RedoLogRecord* redoLogRecord2, uint64_t type, bool system);
        void processDdlHeader(RedoLogRecord* redoLogRecord1);
        virtual void initialize();
        virtual void processCommit(bool system) = 0;
        virtual void processCheckpoint(typeScn scn, typeTime time_, typeSeq sequence, uint64_t offset, bool redo) = 0;
        void releaseBuffers(uint64_t maxId);
        void sleepForWriterWork(uint64_t queueSize, uint64_t nanoseconds);
        void wakeUp();

        friend class SystemTransaction;
    };
}

#endif