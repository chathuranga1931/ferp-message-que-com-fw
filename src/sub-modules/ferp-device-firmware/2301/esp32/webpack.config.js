/// Native Modules
const path = require('path');

/// Node Modules
const webpack = require('webpack');

/// Webpack Configuration
module.exports = {
    mode: 'production',
    entry: './web/lib/index.ts',
    module: { rules: [{ test: /\.ts$/, use: 'ts-loader', exclude: /node_modules/ }] },
    resolve: { extensions: ['.ts', '.js'] },
    plugins: [new webpack.optimize.LimitChunkCountPlugin({ maxChunks: 1 })],
    output: { filename: 'bundle.js', path: path.resolve(__dirname, 'web/build') }
};
