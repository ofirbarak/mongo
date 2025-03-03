/**
 * Test that the bucket unpacking with sorting rewrite is performed when plan is cached or
 * replanned.
 *
 * @tags: [
 *     # Plan cache stats doesn't support different read concerns.
 *     assumes_read_concern_unchanged,
 *     requires_fcv_60,
 *     # We need a timeseries collection.
 *     assumes_no_implicit_collection_creation_after_drop,
 *     # Cannot insert into a time-series collection in a multi-document transaction.
 *     does_not_support_transactions,
 *     # Refusing to run a test that issues an aggregation command with explain because it may
 *     # return incomplete results if interrupted by a stepdown.
 *     does_not_support_stepdowns,
 *     # This complicates aggregation extraction.
 *     do_not_wrap_aggregations_in_facets,
 *     # We need a timeseries collection.
 *     requires_timeseries,
 *     # Explain of a resolved view must be executed by mongos.
 *     directly_against_shardsvrs_incompatible,
 *     # We use the profiler to get info in order to force replanning.
 *     requires_profiling,
 * ]
 */
(function() {
"use strict";

load("jstests/libs/profiler.js");                    // For getLatestProfileEntry.
load("jstests/libs/fixture_helpers.js");             // For FixtureHelpers.
load("jstests/libs/analyze_plan.js");                // For planHasStage.
load("jstests/core/timeseries/libs/timeseries.js");  // For TimeseriesTest.

if (!TimeseriesTest.bucketUnpackWithSortEnabled(db.getMongo())) {
    jsTestLog("Skipping test because 'BucketUnpackWithSort' is disabled.");
    return;
}

const fields = ["a", "b", "i"];

const addDocs = (coll, numDocs, constants = [1, 1]) => {
    let bulk = coll.initializeUnorderedBulkOp();

    assert.eq(fields.length, 3, fields);
    assert.eq(constants.length, 2, constants);

    // `m.a` & `m.b` will have value constants[i]. `m.i` & `t` will have value `i`.
    // `m.i` is to create separate buckets.
    for (let i = 0; i < numDocs; ++i) {
        let meta = {[fields[0]]: constants[0], [fields[1]]: constants[1], [fields[2]]: i};
        bulk.insert({m: meta, t: new Date(i)});
    }

    assert.commandWorked(bulk.execute());
};
const setupCollection = (coll, collName, numDocs) => {
    coll.drop();
    db.createCollection(collName, {timeseries: {timeField: "t", metaField: "m"}});

    addDocs(coll, numDocs);
};

const collName = jsTestName();
const coll = db[collName];
const bucketsName = "system.buckets." + collName;
const stageName = "$_internalBoundedSort";
const bucketsColl = db[bucketsName];

const numDocs = 20;
// Setup with a few documents.
setupCollection(coll, collName, numDocs);

// Create indexes so that we have something to multiplan.
assert.commandWorked(coll.createIndex({"m.a": 1, "m.i": 1, t: 1}));
assert.commandWorked(coll.createIndex({"m.b": 1, "m.i": 1, t: 1}));

// Check that the rewrite is performed before caching.
const pipeline = [{$sort: {"m.i": 1, t: 1}}, {$match: {"m.a": 1, "m.b": 1}}];
let explain = coll.explain().aggregate(pipeline);
assert.eq(getAggPlanStages(explain, stageName).length, 1, explain);

// Check the cache is empty.
assert.eq(db[bucketsName].getPlanCache().list().length, 0);

// Run in order to cache the plan.
let result = coll.aggregate(pipeline).toArray();
assert.eq(result.length, 20, result);

// Check the answer was cached.
assert.eq(db[bucketsName].getPlanCache().list().length, 1);

// Check that the solution still uses internal bounded sort.
explain = coll.explain().aggregate(pipeline);
assert(getAggPlanStages(explain, stageName).length === 1, explain);

// Get constants needed for replanning.
const cursorStageName = "$cursor";
const planCacheKey =
    getPlanCacheKeyFromExplain(getAggPlanStage(explain, cursorStageName)[cursorStageName], db);
const planCacheEntry = (() => {
    const planCache = bucketsColl.getPlanCache().list([{$match: {planCacheKey}}]);
    assert.eq(planCache.length, 1, planCache);
    return planCache[0];
})();
let ratio = (() => {
    const getParamRes = assert.commandWorked(
        db.adminCommand({getParameter: 1, internalQueryCacheEvictionRatio: 1}));
    return getParamRes["internalQueryCacheEvictionRatio"];
})();

// Remove existing docs, add docs to trigger replanning.
assert.commandWorked(coll.deleteMany({"m.a": 1, "m.b": 1}));
let numNewDocs = ratio * planCacheEntry.works + 1;
addDocs(coll, numNewDocs, [1, 0]);
addDocs(coll, numNewDocs, [0, 1]);

// Turn on profiling.
db.setProfilingLevel(2);

// Rerun command with replanning.
const comment = jsTestName();
result = coll.aggregate(pipeline, {comment}).toArray();
assert.eq(result.length, 0);

// Check that the plan was replanned.
const replanProfileEntry = getLatestProfilerEntry(db, {'command.comment': comment});
assert(replanProfileEntry.replanned, replanProfileEntry);

// Check that rewrite happens with replanning.
explain = coll.explain().aggregate(pipeline);
assert(getAggPlanStages(explain, stageName).length === 1,
       {explain, stages: getAggPlanStages(explain, stageName)});
})();
