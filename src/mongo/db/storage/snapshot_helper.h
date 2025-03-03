/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/db/operation_context.h"

namespace mongo {
namespace SnapshotHelper {
struct ReadSourceChange {
    boost::optional<RecoveryUnit::ReadSource> newReadSource;
    bool shouldReadAtLastApplied;
};

/**
 * Returns a ReadSourceChange containing data necessary to decide if we need to change read source.
 *
 * For Lock-Free Reads, the decisions made within this function based on replication state may
 * become invalid after it returns and multiple calls may yield different answers. Higher level code
 * must validate the relevance of the outcome based on replication state before and after calling
 * this function.
 */
ReadSourceChange shouldChangeReadSource(OperationContext* opCtx, const NamespaceString& nss);

/**
 * Returns true if 'collectionMin' is not compatible with 'readTimestamp'. They are incompatible
 * when the read timestamp is older than the latest in-memory collection state: the storage engine
 * view would not match the in-memory collection state.
 */
bool collectionChangesConflictWithRead(boost::optional<Timestamp> collectionMin,
                                       boost::optional<Timestamp> readTimestamp);
}  // namespace SnapshotHelper
}  // namespace mongo
