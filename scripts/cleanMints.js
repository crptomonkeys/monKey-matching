const { getApi, eosRpc } = require('./utils/api');
const config = require('./config.json');
const cache = require('./mint.cache.json');
const fetch = require('node-fetch');
const actionPerTx = 50;

const action = async (indexes) => {
    const endpoint = endpoints[Math.floor(Math.random() * endpoints.length)];
    console.log('Using endpoint', endpoint);
    const api = getApi(endpoint, config.auth.key);

    return api.transact({
        actions: indexes.map(x => ({
            account: config.target.contract,
            name: 'rmmint',

            authorization: [{
                actor: config.auth.address,
                permission: config.auth.permission,
            }],

            data: {
                index: x,
            },
        }))

    }, {
        blocksBehind: 3,
        expireSeconds: 30,
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
            limit: 91, // limit selection & paginate due to node constraints
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

(async () => {
    endpoints = config.endpoints.wax ?? (await fetch('http://waxmonitor.cmstats.net/api/endpoints?format=json&type=api').then(x => x.json())).filter(({ weight }) => weight > 5).map(({ node_url }) => node_url);
    console.log('Got endpoints', endpoints);

    oldData = await getExistingData();

    const toProcess = [];

    const size = oldData.length;
    for (let i = 0; i < size; i += actionPerTx) {
        toProcess.push(
            oldData.slice(i, i + actionPerTx).map(x => x.index)
        );
    }

    for (let index = 0; index < toProcess.length; index) {
        console.log('Cleaning...', index, '/', toProcess.length);

        await action(toProcess[index])
            .then(() => index++)
            .catch(x => console.log('Got error, retrying', x));
    }
})();