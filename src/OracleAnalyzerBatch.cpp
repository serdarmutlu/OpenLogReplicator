/* Thread reading Oracle Redo Logs using batch mode
   Copyright (C) 2018-2021 Adam Leszczynski (aleszczynski@bersler.com)

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

#include "OracleAnalyzerBatch.h"
#include "RuntimeException.h"

using namespace std;

extern void stopMain();

namespace OpenLogReplicator {
    OracleAnalyzerBatch::OracleAnalyzerBatch(OutputBuffer *outputBuffer, uint64_t dumpRedoLog, uint64_t dumpRawData, const char *alias,
            const char *database, uint64_t memoryMinMb, uint64_t memoryMaxMb, uint64_t readBufferMax, uint64_t disableChecks,
            typeCONID conId) :
        OracleAnalyzer(outputBuffer, dumpRedoLog, dumpRawData, alias, database, memoryMinMb, memoryMaxMb, readBufferMax, disableChecks) {

        this->conId = conId;
    }

    OracleAnalyzerBatch::~OracleAnalyzerBatch() {
    }

    void OracleAnalyzerBatch::positionReader(void) {
        if (startSequence > 0) {
            sequence = startSequence;
            firstScn = 0;
        } else if (startTime.length() > 0) {
            RUNTIME_FAIL("starting by time is not supported for batch mode");
        } else if (startTimeRel > 0) {
            RUNTIME_FAIL("starting by relative time is not supported for batch mode");
        } else if (startScn != ZERO_SCN) {
            sequence = 0;
            firstScn = startScn;
        } else {
            sequence = 0;
            firstScn = 0;
        }
    }

    void OracleAnalyzerBatch::createSchema(void) {
        if ((flags & REDO_FLAGS_SCHEMALESS) != 0)
            return;

        WARNING("HINT: if you don't have earlier schema, try with schema-less mode (\"flags\": 2)");
        if (schemaFirstScn != ZERO_SCN) {
            WARNING("HINT: you can also set start SCN for writer: \"start-scn\": " << dec << schemaFirstScn);
        }

        RUNTIME_FAIL("schema file missing");
    }

    const char* OracleAnalyzerBatch::getModeName(void) const {
        return "batch";
    }

    bool OracleAnalyzerBatch::continueWithOnline(void) {
        INFO("finished batch processing, exiting");
        stopMain();
        return false;
    }
}
