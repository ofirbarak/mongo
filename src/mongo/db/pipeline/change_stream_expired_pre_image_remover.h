/**
 *    Copyright (C) 2021-present MongoDB, Inc.
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

#include "mongo/db/change_stream_options_gen.h"
#include "mongo/db/service_context.h"

namespace mongo {
namespace preImageRemoverInternal {

/**
 * Specifies attributes that determines if the pre-image has been expired or not.
 */
struct PreImageAttributes {
    mongo::UUID collectionUUID;
    Timestamp ts;
    Date_t operationTime;

    /**
     * Determines if the pre-image is considered expired based on the expiration parameter being
     * set.
     */
    bool isExpiredPreImage(const boost::optional<Date_t>& preImageExpirationTime,
                           const Timestamp& earliestOplogEntryTimestamp);
};

boost::optional<Date_t> getPreImageExpirationTime(OperationContext* opCtx, Date_t currentTime);

}  // namespace preImageRemoverInternal

/**
 * Starts a periodic background job to remove expired documents from 'system.preimages' collection.
 */
void startChangeStreamExpiredPreImagesRemover(ServiceContext* serviceContext);

/**
 * Stops the periodic background job that removes expired documents from 'system.preimages'
 * collection.
 */
void shutdownChangeStreamExpiredPreImagesRemover(ServiceContext* serviceContext);
}  // namespace mongo
