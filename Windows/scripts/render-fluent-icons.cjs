// Legacy resource-generation entry point. Current masters are the user's
// refined Arabic calligraphy; this does not build/package the application.
const path = require('path');
require('child_process').execFileSync(process.env.SUKUT_PYTHON || 'python', [path.join(__dirname, 'encode-sukut-icons.py')], {stdio:'inherit'});
