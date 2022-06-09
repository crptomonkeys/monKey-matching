const { getApi, eosRpc } = require('./utils/api');
const config = require('./config.json');
const cache = require('./mint.cache.json');
const fetch = require('node-fetch');
const actionPerTx = 25;

const groups = {};

// groups
const chunks = [];
const chunkSize = 100;

cache.forEach(x => {
    if (groups[x.template_id] === undefined) {
        groups[x.template_id] = [];
    }

    groups[x.template_id].push(x);
});

for (const key in groups) {
    const value = groups[key].sort((x, y) => parseInt(x.asset_id) - parseInt(y.asset_id));

    const size = value.length;
    for (let i = 0; i < size; i += chunkSize) {
        chunks.push({
            template_id: key,
            mints: value.slice(i, i + chunkSize).map(x => ({
                asset_id: x.asset_id,
                mint: x.mint,
            })),
        });
    }
};
console.log(`Chunked data into ${chunks.length} chunks`);

let endpoints = [];

const action = async (data) => {
    const endpoint = endpoints[Math.floor(Math.random() * endpoints.length)];
    console.log('Using endpoint', endpoint);
    const api = getApi(endpoint, config.auth.key);

    return api.transact({
        actions: data.map(([{ template_id, mints }, index]) => ({
            account: config.target.contract,
            name: 'addmint',

            authorization: [{
                actor: config.auth.address,
                permission: config.auth.permission,
            }],

            data: {
                index,
                template_id: template_id,
                new_mints: mints,
            },
        }))
    }, {
        blocksBehind: 3,
        expireSeconds: 120,
    });
};

const getExistingData = async () => {
    const endpoint = endpoints[Math.floor(Math.random() * endpoints.length)];
    console.log('Using endpoint', endpoint);

    let result;
    let allRows = [];
    do {
        result = await eosRpc(endpoint).get_table_rows({
            code: config.target.contract,
            scope: config.target.contract,
            table: 'mints',
            limit: 25, // limit selection & paginate due to node constraints
            ...(result != undefined && {
                lower_bound: result.next_key
            })
        });
        console.log("Got #results", result?.rows?.length, "has more", result?.more);

        allRows = [...allRows, ...result?.rows];
    } while (result?.more)

    console.log("Got all count:", allRows.length);

    return allRows;
};

const arraysMatch = (arr1, arr2) => {
    if (arr1.length !== arr2.length) return false;

    for (var i = 0; i < arr1.length; i++) {
        if ((parseInt(arr1[i].asset_id) !== parseInt(arr2[i].asset_id)) || (parseInt(arr1[i].mint) !== parseInt(arr2[i].mint))) return false;
    }

    return true;
};

(async () => {
    endpoints = config.endpoints.wax ?? (await fetch('http://waxmonitor.cmstats.net/api/endpoints?format=json&type=api').then(x => x.json())).filter(({ weight }) => weight > 5).map(({ node_url }) => node_url);
    console.log('Got endpoints', endpoints);

    oldData = await getExistingData();

    const templateIndexStart = 10000000000; // max int32 = 2,147,483,647

    let templateIndex = -1;
    let prevTemplateId = -1;
    let newData = [];
    for (let index = 0; index < chunks.length; index++) {
        // template id changed, reset templateIndex
        if (prevTemplateId !== chunks[index].template_id) {
            templateIndex = 0;
            prevTemplateId = chunks[index].template_id;
        }
        else {
            // increment
            templateIndex++;
        }

        // key is composited of (templateIndex * templateIndexStart) + template_id
        // where templateIndex = the nth time the same template is being used
        // templateIndexStart = value vigger than max int32
        // template_id = the template id (int32)
        // eg templateIndex = 0, templateIndexStart = 10000000000 and template_id = 1234
        // => 1234
        // eg templateIndex = 0, templateIndexStart = 10000000000 and template_id = 1234
        // => 10000001234
        const key = parseInt(templateIndex * templateIndexStart) + parseInt(chunks[index].template_id);
        console.log('Checking key', key);

        // check if the key already exists on chain
        const matches = oldData.filter(x => parseInt(x.index) === key);

        // no match found
        if (matches.length === 0) {
            // do nothing special
            console.log('No key match found', key);
            
            newData.push([chunks[index], key]);
        }
        else if (!arraysMatch(matches[0].mints, chunks[index].mints)) {
            // validate if current data == old data
            // matches, we can ignore this
            console.log("Data is out-of-date", key);
            newData.push([chunks[index], key]);
        }
    }

    const toProcess = [];

    const size = newData.length;
    for (let i = 0; i < size; i += actionPerTx) {
        toProcess.push(
            newData.slice(i, i + actionPerTx)
        );
    }

    for (let index = 0; index < toProcess.length; index) {
        console.log('Pushing...', index, '/', toProcess.length);

        await action(toProcess[index])
            .then(() => index++)
            .catch(x => console.log('Got error, retrying', x));
    }
})();