const express = require('express');
const bodyParser = require('body-parser');
const path = require('path');
const fs = require('fs');
const app = express();
const port = 8080;

app.use(express.static(path.join(__dirname, '../data')));
app.use(bodyParser.json());

app.get('/', (req, res) => {
    res.redirect('/home.html');
});

app.get('/config', (_, res) => {
    console.log('Loading configuration');
    let configPath = path.join(__dirname, 'data/config.json');
    try {
        // Read the configuration file.
        fs.accessSync(configPath, fs.constants.R_OK);
        fs.readFile(configPath, (err, data) => {
            if (err) {
                console.error('Unable to read configuration: ', err);
                res.status(500).send('Unable to read configuration');
            } else {
                let config = JSON.parse(data);
                res.status(200).send(JSON.stringify(config));
            }
        });
    } catch (err) {
        // Return defaults instead.
        console.debug('Returning default configuration', err);
        let newConfig = {
            deviceName: "Test Clock",
            alarmTime: 360,
            alarmActivation: 'ALARM_DISABLED',
            radioFrequency: 99.3,
            brightness: 15,
            dayColour: [255, 255, 255],
            nightColour: [255, 0, 0],
            alarmColour: [0, 0, 255],
            dayPattern: 'RAINBOW_DIGITS',
            nightPattern: 'SOLID_COLOUR',
            alarmPattern: 'RAINBOW_DIGITS',
            latitude: -31.9514,
            longitude: 115.8617,
            isRadioInstalled: true,
            is24Hour: true,
            isUseRadio: false
        };
        fs.writeFileSync(configPath, JSON.stringify(newConfig, null, '  '));
        res.status(200).send(newConfig);
    }
});

app.post('/writeConfig', (req, res) => {
    let configPath = path.join(__dirname, 'data/config.json');
    fs.readFile(configPath, (err, data) => {
        if (err) {
            console.error('Unable to read config for name write: ', err);
            res.status(500).send('Unable to write configuration');
        } else {
            let config = JSON.parse(data);
            let newConfig = JSON.stringify({
                ...config,
                deviceName: req.body.deviceName,
                alarmTime: req.body.alarmTime,
                alarmActivation: req.body.alarmActivation,
                radioFrequency: req.body.radioFrequency,
                brightness: req.body.brightness,
                dayColour: req.body.dayColour,
                nightColour: req.body.nightColour,
                alarmColour: req.body.alarmColour,
                dayPattern: req.body.dayPattern,
                nightPattern: req.body.nightPattern,
                alarmPattern: req.body.alarmPattern,
                latitude: req.body.latitude,
                longitude: req.body.longitude,
                timezone: req.body.timezone,
                offset: req.body.offset,
                isRadioInstalled: req.body.isRadioInstalled,
                is24Hour: req.body.is24Hour,
                isUseRadio: req.body.isUseRadio
            }, null, '  ');
            fs.writeFile(configPath, newConfig, (err2) => {
                if (err2) {
                    console.error('Unale to write configuration: ', err2);
                    res.status(500).send('Unable to write configuration');
                } else {
                    console.log('Configuration written');
                    res.sendStatus(200);
                }
            });
        }
    });
});

var server = app.listen(port, () => {
    console.log(`Test server running at http://localhost:${port}`);
});