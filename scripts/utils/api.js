const { Api, JsonRpc } = require('eosjs');
const { JsSignatureProvider } = require('eosjs/dist/eosjs-jssig');
const fetch = require('node-fetch');
const { TextEncoder, TextDecoder } = require('util');

// Create api instance given a private key
const getApi = (endpoint, privateKey) => new Api({
    rpc: eosRpc(endpoint),
    signatureProvider: new JsSignatureProvider([privateKey]),
    textDecoder: new TextDecoder(),
    textEncoder: new TextEncoder()
});

const eosRpc = (endpoint) => new JsonRpc(endpoint, { fetch });

module.exports = {
    getApi,
    eosRpc,
};