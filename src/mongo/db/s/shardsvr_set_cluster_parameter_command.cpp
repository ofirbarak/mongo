/**
 *    Copyright (C) 2022-present MongoDB, Inc.
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

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/cancelable_operation_context.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/set_cluster_parameter_invocation.h"
#include "mongo/db/commands/user_management_commands_gen.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/repl/repl_client_info.h"
#include "mongo/logv2/log.h"
#include "mongo/s/grid.h"
#include "mongo/s/request_types/sharded_ddl_commands_gen.h"

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kSharding


namespace mongo {
namespace {

MONGO_FAIL_POINT_DEFINE(hangInShardsvrSetClusterParameter);

const WriteConcernOptions kLocalWriteConcern{
    1, WriteConcernOptions::SyncMode::UNSET, WriteConcernOptions::kNoTimeout};

class ShardsvrSetClusterParameterCommand final
    : public TypedCommand<ShardsvrSetClusterParameterCommand> {
public:
    using Request = ShardsvrSetClusterParameter;

    class Invocation final : public InvocationBase {
    public:
        using InvocationBase::InvocationBase;

        void typedRun(OperationContext* opCtx) {
            uassert(ErrorCodes::IllegalOperation,
                    str::stream() << Request::kCommandName << " can only be run on shard servers",
                    serverGlobalParams.clusterRole == ClusterRole::ShardServer);
            CommandHelpers::uassertCommandRunWithMajority(Request::kCommandName,
                                                          opCtx->getWriteConcern());

            hangInShardsvrSetClusterParameter.pauseWhileSet();

            SetClusterParameter setClusterParameterRequest(request().getCommandParameter());
            setClusterParameterRequest.setDbName(NamespaceString::kAdminDb);
            std::unique_ptr<ServerParameterService> parameterService =
                std::make_unique<ClusterParameterService>();
            DBDirectClient client(opCtx);
            ClusterParameterDBClientService dbService(client);
            SetClusterParameterInvocation invocation{std::move(parameterService), dbService};
            // Use local write concern for setClusterParameter, the idea is that the command is
            // being called with majority write concern, so, we'll wait for majority after checking
            // out the session.
            bool writePerformed = invocation.invoke(opCtx,
                                                    setClusterParameterRequest,
                                                    request().getClusterParameterTime(),
                                                    kLocalWriteConcern);
            if (!writePerformed) {
                // Since no write happened on this txnNumber, we need to make a dummy write so
                // that secondaries can be aware of this txn.
                DBDirectClient client(opCtx);
                client.update(NamespaceString::kServerConfigurationNamespace.ns(),
                              BSON("_id"
                                   << "SetClusterParameterStats"),
                              BSON("$inc" << BSON("count" << 1)),
                              true /* upsert */,
                              false /* multi */);
            }
        }

    private:
        NamespaceString ns() const override {
            return NamespaceString();
        }

        bool supportsWriteConcern() const override {
            return true;
        }

        void doCheckAuthorization(OperationContext* opCtx) const override {
            uassert(ErrorCodes::Unauthorized,
                    "Unauthorized",
                    AuthorizationSession::get(opCtx->getClient())
                        ->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(),
                                                           ActionType::internal));
        }
    };

    std::string help() const override {
        return "Internal command, which is exported by the shard servers. Do not call "
               "directly. Set's the cluster parameter in the node.";
    }

    bool adminOnly() const override {
        return true;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }
} shardsvrSetClusterParameterCmd;

}  // namespace
}  // namespace mongo
