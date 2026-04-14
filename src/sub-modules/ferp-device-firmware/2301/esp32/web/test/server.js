// modules
const path = require('path');
const express = require('express');

// application constants
const app = express();
const port = 3000;

// allowing URL parameters
app.use(express.urlencoded({ extended: true }));

// exposing static files
app.use(express.static(path.join(__dirname, '../build')));
app.use('/html', express.static(path.join(__dirname, '../html')));

// application paths
app.get('/', (_, res) => res.sendFile(path.join(__dirname, '../index.html')));

/***************
 *  API PATHS  *
 ***************/

// basic status
app.post('/ajax/user', (req, res) =>
    req.body.request === 'data' ? res.json({ ssid: '', password: '' }) : res.json({ result: 'success' })
);

// begin listening
app.listen(port, () => console.log(`AGI-Drive > Listening at http://localhost:${port}`));
