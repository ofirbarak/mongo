/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/db/initialize_operation_session_info.h"

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/internal_transactions_feature_flag_gen.h"
#include "mongo/db/logical_session_cache.h"
#include "mongo/db/logical_session_id_helpers.h"
#include "mongo/db/operation_context.h"

namespace mongo {

OperationSessionInfoFromClient initializeOperationSessionInfo(OperationContext* opCtx,
                                                              const BSONObj& requestBody,
                                                              bool requiresAuth,
                                                              bool attachToOpCtx,
                                                              bool isReplSetMemberOrMongos) {
    auto osi = OperationSessionInfoFromClient::parse("OperationSessionInfo"_sd, requestBody);
    auto isAuthorizedForInternalClusterAction =
        AuthorizationSession::get(opCtx->getClient())
            ->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(),
                                               ActionType::internal);

    if (opCtx->getClient()->isInDirectClient()) {
        uassert(50891,
                "Invalid to set operation session info in a direct client",
                !osi.getSessionId() && !osi.getTxnNumber() && !osi.getAutocommit() &&
                    !osi.getStartTransaction());
    }

    if (!requiresAuth) {
        uassert(ErrorCodes::OperationNotSupportedInTransaction,
                "This command is not supported in transactions",
                !osi.getAutocommit());
        uassert(
            50889, "It is illegal to provide a txnNumber for this command", !osi.getTxnNumber());
    }

    if (auto authSession = AuthorizationSession::get(opCtx->getClient())) {
        // If we're using the localhost bypass, and the client hasn't authenticated,
        // logical sessions are disabled. A client may authenticate as the __sytem user,
        // or as an externally authorized user.
        if (authSession->isUsingLocalhostBypass() && !authSession->isAuthenticated()) {
            return {};
        }

        // Do not initialize lsid when auth is enabled and no user is logged in since
        // there is no sensible uid that can be assigned to it.
        if (AuthorizationManager::get(opCtx->getServiceContext())->isAuthEnabled() &&
            !authSession->isAuthenticated() && !requiresAuth) {
            return {};
        }
    }

    if (osi.getSessionId()) {
        stdx::lock_guard<Client> lk(*opCtx->getClient());

        auto lsc = LogicalSessionCache::get(opCtx->getServiceContext());
        if (!lsc) {
            // Ignore session information if the logical session cache has not been set up, e.g. on
            // the embedded version of mongod.
            return {};
        }

        // If osi lsid includes the uid, makeLogicalSessionId will also verify that the hash
        // matches with the current user logged in.
        auto lsid = makeLogicalSessionId(osi.getSessionId().get(), opCtx);

        if (!attachToOpCtx) {
            return {};
        }

        if (isChildSession(lsid)) {
            uassert(ErrorCodes::InvalidOptions,
                    "Internal sessions are only allowed for internal clients",
                    isAuthorizedForInternalClusterAction);
            uassert(ErrorCodes::InvalidOptions,
                    "Internal sessions are not supported outside of transactions",
                    osi.getTxnNumber() && osi.getAutocommit() && !osi.getAutocommit().value());
        }

        opCtx->setLogicalSessionId(std::move(lsid));
        uassertStatusOK(lsc->vivify(opCtx, opCtx->getLogicalSessionId().get()));
    } else {
        uassert(ErrorCodes::InvalidOptions,
                "Transaction number requires a session ID to also be specified",
                !osi.getTxnNumber());
    }

    if (osi.getTxnNumber()) {
        invariant(osi.getSessionId());
        stdx::lock_guard<Client> lk(*opCtx->getClient());

        uassert(ErrorCodes::IllegalOperation,
                "Transaction numbers are only allowed on a replica set member or mongos",
                isReplSetMemberOrMongos);
        uassert(ErrorCodes::InvalidOptions,
                "Transaction number cannot be negative",
                *osi.getTxnNumber() >= 0);

        opCtx->setTxnNumber(*osi.getTxnNumber());

        if (auto txnRetryCounter = osi.getTxnRetryCounter()) {
            uassert(ErrorCodes::InvalidOptions,
                    "txnRetryCounter is only allowed for internal clients",
                    isAuthorizedForInternalClusterAction);
            uassert(ErrorCodes::InvalidOptions,
                    str::stream() << "Cannot specify txnRetryCounter for a retryable write",
                    osi.getAutocommit().has_value());
            uassert(ErrorCodes::InvalidOptions,
                    "txnRetryCounter cannot be negative",
                    txnRetryCounter >= 0);
            opCtx->setTxnRetryCounter(*txnRetryCounter);
        }
    } else {
        uassert(ErrorCodes::InvalidOptions,
                "'autocommit' field requires a transaction number to also be specified",
                !osi.getAutocommit());
    }

    if (osi.getAutocommit()) {
        invariant(osi.getTxnNumber());
        uassert(ErrorCodes::InvalidOptions,
                "Specifying autocommit=true is not allowed.",
                !osi.getAutocommit().value());
        opCtx->setInMultiDocumentTransaction();
    } else {
        uassert(ErrorCodes::InvalidOptions,
                "'startTransaction' field requires 'autocommit' field to also be specified",
                !osi.getStartTransaction());
    }

    if (osi.getStartTransaction()) {
        invariant(osi.getAutocommit());
        uassert(ErrorCodes::InvalidOptions,
                "Specifying startTransaction=false is not allowed.",
                osi.getStartTransaction().value());
    }

    return osi;
}

}  // namespace mongo
