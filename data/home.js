// Timer used for the popup notifications.
let timer = undefined;

function setupPatternListener(patternElemName, colourElemName) {
    const colourElem = document.getElementById(colourElemName);
    document.getElementById(patternElemName).addEventListener("change", (e) => {
        if (e.target.value.includes("RAINBOW")) {
            colourElem.disabled = true;
        } else {
            colourElem.disabled = false;
        }
    });
}

function checkPatterns() {
    const prefixes = ["day", "night", "alarm"];
    for (let index in prefixes) {
        const prefix = prefixes[index];
        const ev = new InputEvent("change", {
            target: document.getElementById(prefix + "Pattern")
        });

        document.getElementById(prefix + "Pattern").dispatchEvent(ev);
    }
}

function setup() {
    // Set up the birghtness value updates.
    const bv = document.getElementById("brightnessValue");
    const inp = document.getElementById("brightness");
    inp.addEventListener("input", (e) => bv.innerHTML = e.target.value);
    bv.innerHTML = inp.value;

    // Set up the pattern updates.
    setupPatternListener("dayPattern", "dayColour");
    setupPatternListener("nightPattern", "nightColour");
    setupPatternListener("alarmPattern", "alarmColour");

    // Load the initial configuration.
    loadConfiguration();
}

function loadConfiguration() {
    let xhr = new XMLHttpRequest();
    xhr.addEventListener("load", function() {
        if (xhr.status !== 200) {
            showNotification("Unable to retrieve configuration, status = " + xhr.status, "error");
        } else {
            // Get the configuration.
            let json = JSON.parse(xhr.responseText);
            if (json.deviceName !== undefined) {
                document.getElementById("deviceName").value = json.deviceName;
            }
            const alarmTime = json.alarmTime || 0;
            document.getElementById("alarmTime").value = 
                zeroPad(Math.floor(alarmTime / 60), 2) + ":" +
                zeroPad(alarmTime % 60, 2);
            const alarmActivation = json.alarmActivation || "";
            document.getElementById("alarmActivation").value = alarmActivation;
            const isAlarmDisabled = json.isAlarmDisabled || false;
            if (isAlarmDisabled) {
                document.getElementById("radioSettings").classList.add("Hidden");
                document.getElementById("alarm").classList.add("Hidden");
                document.getElementById("alarmDisplay").classList.add("Hidden");
            } else {
                document.getElementById("alarm").classList.remove("Hidden");
                document.getElementById("alarmDisplay").classList.remove("Hidden");
                const isRadioInstalled = json.isRadioInstalled || false;
                if (!isRadioInstalled) {
                    document.getElementById("radioSettings").classList.add("Hidden");
                } else {
                    document.getElementById("radioSettings").classList.remove("Hidden");
                    const radioFrequency = json.radioFrequency || 88.0;
                    document.getElementById("radioFrequency").value = radioFrequency;
                    const isUseRadio = json.isUseRadio || false;
                    document.getElementById("useRadio").checked = isUseRadio;
                    document.getElementById("useBuzzer").checked = !isUseRadio;
                }
            }

            const brightness = json.brightness || 15;
            document.getElementById("brightness").value = brightness;
            document.getElementById("brightnessValue").innerHTML = brightness;

            const is24Hour = json.is24Hour || true;
            document.getElementById("twelveHour").checked = !is24Hour;
            document.getElementById("twentyFourHour").checked = is24Hour;

            const latitude = json.latitude || 0;
            document.getElementById("latitude").value = latitude;
            
            const longitude = json.longitude || 0;
            document.getElementById("longitude").value = longitude;

            const timezone = json.timezone || "";
            document.getElementById("timezone").value = timezone;

            const offset = json.offset || 0;
            document.getElementById("offset").value = offset;

            const dayPattern = json.dayPattern || "SOLID_COLOUR";
            document.getElementById("dayPattern").value = dayPattern;
            document.getElementById("dayColour").value = colourArrayToHtmlColour(json.dayColour || "");

            const nightPattern = json.nightPattern || "SOLID_COLOUR";
            document.getElementById("nightPattern").value = nightPattern;
            document.getElementById("nightColour").value = colourArrayToHtmlColour(json.nightColour || "");

            const alarmPattern = json.alarmPattern || "SOLID_COLOUR";
            document.getElementById("alarmPattern").value = alarmPattern;
            document.getElementById("alarmColour").value = colourArrayToHtmlColour(json.alarmColour || "");

            const version = json.version || "unknown";
            document.getElementById("version").textContent = version;

            // Update the pattern/colour enablement.
            checkPatterns();
        }
    });
    xhr.addEventListener("error", function(e) {
        showNotification("Unable to retrieve configuration", "error");
    });
    xhr.open("GET", "/config");
    xhr.setRequestHeader('Cache-Control', 'no-cache');
    xhr.send();
}

function saveConfiguration(config = "", callback = undefined) {
    let json = config;
    if (json === undefined || json.trim() === "") {
        json = configurationToJson();
    }
    
    let xhr = new XMLHttpRequest();
    xhr.addEventListener("load", function(e) {
        if (xhr.status !== 200) {
            showNotification("Unable to save configuration, status = " + xhr.status, "error");
            console.error("Save configuration failed:", e);
        } else {
            if (callback) {
                callback();
            } else {
                showNotification("Configuration saved successfully", "success");
            }
        }
    });
    xhr.addEventListener("error", function(e) {
        showNotification("Unable to save configuration, status = " + xhr.status, "error");
        console.error("Save configuration failed:", e);
    });
    xhr.open("POST", "/writeConfig");
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send(json);
}

function configurationToJson() {
    let msg = {};
    let alarmTime = document.getElementById("alarmTime").value || "";
    if (alarmTime.length < 5) {
        alarmTime = "00:00";
    }

    msg.deviceName = document.getElementById("deviceName").value;

    const alarmHour = parseInt(alarmTime.substring(0, 2), 10) % 24;
    const alarmMinute = parseInt(alarmTime.substring(3), 10) % 60;
    msg.alarmTime = (alarmHour * 60) + alarmMinute;
    msg.alarmActivation = document.getElementById("alarmActivation").value;
    msg.isRadioInstalled = !document.getElementById("radioSettings").classList.contains("Hidden");
    if (msg.isRadioInstalled) {
        msg.radioFrequency = parseFloat(document.getElementById("radioFrequency").value);
        msg.isUseRadio = document.getElementById("useRadio").checked;
    }

    msg.brightness = document.getElementById("brightness").value;
    msg.is24Hour = document.getElementById("twentyFourHour").checked;
    msg.latitude = document.getElementById("latitude").value;
    msg.longitude = document.getElementById("longitude").value;
    msg.timezone = document.getElementById("timezone").value;
    msg.offset = document.getElementById("offset").value;
    msg.dayPattern = document.getElementById("dayPattern").value;
    msg.dayColour = htmlColourToColourArray(document.getElementById("dayColour").value);
    msg.nightPattern = document.getElementById("nightPattern").value;
    msg.nightColour = htmlColourToColourArray(document.getElementById("nightColour").value);
    msg.alarmPattern = document.getElementById("alarmPattern").value;
    msg.alarmColour = htmlColourToColourArray(document.getElementById("alarmColour").value);
    return JSON.stringify(msg, null, 2);
}

/**
 * Hides the backup modal dialog.
 */
function hideBackup() {
    document.getElementById("modalBackground").classList.remove("Show");
}

/**
 * Shows the backup modal dialog.
 */
function showBackup() {
    loadConfiguration();
    document.getElementById("backupText").value = configurationToJson();
    document.getElementById("backupConfiguration").classList.remove("Hidden");
    document.getElementById("modalBackground").classList.add("Show");
}

/**
 * Verifies the display section of JSON is valid.
 * 
 * @param {*} prefix The prefix (day|night|alarm) to be checked.
 * @param {*} pattern The pattern value to be validated.
 * @param {*} colour The colour value to be validated.
 * @param {*} errors List into which any errors are written.
 */
function verifyDisplay(prefix, pattern, colour, errors) {
    let found = false;
    if (pattern instanceof String ||
        typeof pattern === "string") {
        const patterns = document.getElementById(prefix + "Pattern").options;
        const trimPattern = pattern.trim();
        for (let ii = 0; ii < patterns.length; ii++) {
            if (patterns[ii].value === trimPattern) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        errors.push("Invalid " + prefix + " pattern");
    }

    if (!pattern.includes("RAINBOW")) {
        if (!Array.isArray(colour) ||
            colour.length !== 3 ||
            !(colour[0] instanceof Number || typeof colour[0] === "number") ||
            !(colour[1] instanceof Number || typeof colour[1] === "number") ||
            !(colour[2] instanceof Number || typeof colour[2] === "number") ||
            colour[0] < 0 || colour[0] > 255 ||
            colour[1] < 0 || colour[1] > 255 ||
            colour[2] < 0 || colour[2] > 255) {

            errors.push("Invalid " + prefix + " colour");
        }
    }
}

/**
 * Uploads the backup configuration to the microcontroller.
 */
async function uploadBackupConfiguration() {
    // Make sure there's something to use.
    const backupText = document.getElementById("backupText").value.trim();
    if (backupText === "") {
        showNotification("Please paste in some backup text", "error");
        return;
    }

    // Convert the backup JSON to an object.
    let backup = {};
    try {
        backup = JSON.parse(backupText);
    } catch (err) {
        showNotification("Invalid backup text: " + err.name + "<br>" + err.message, "error");
        return;
    }

    // Make sure the object has what we want.
    let errors = [];
    if (backup.deviceName === undefined ||
        backup.deviceName.trim() === "" ||
        backup.deviceName.length > 20) {
        errors.push("Invalid deviceName");
    }
    if (!backup.alarmTime instanceof Number || 
        backup.alarmTime < 0 ||
        backup.alarmTime >= 1440) {
        errors.push("Bad alarm time")
    }
    const activationOptions = document.getElementById("alarmActivation").options;
    let found = false;
    for (let ii = 0; ii < activationOptions.length; ii++) {
        if (activationOptions[ii].value === backup.alarmActivation.trim()) {
            found = true;
            break;
        }
    }
    if (!found) {
        errors.push("Missing alarm activation");
    }
    if (!backup.isRadioInstalled instanceof Boolean) {
        errors.push("Misisng radio installed");
    } else if (backup.isRadioInstalled) {
        if (backup.radioFrequency instanceof Number || 
            backup.radioFrequency < 88.0 ||
            backup.radioFrequency > 107.9) {
            errors.push("Invalid radio frequency");
        }
        if (!backup.isUseRadio instanceof Boolean) {
            errors.push("Invalid use radio flag");
        }
    }
    if (!backup.brightness instanceof Number ||
        backup.brightness < 1 ||
        backup.brightness > 15) {
        errors.push("Invalid brightness")
    }
    if (!backup.is24Hour instanceof Boolean) {
        errors.push("Invalid 24 hour flag");
    }
    if (!backup.latitude instanceof Number ||
        backup.latitude < -90.0 ||
        backup.latitude > 90.0) {
        errors.push("Invalid latitude");
    }
    if (!backup.longitude instanceof Number ||
        backup.longitude < -180.0 ||
        backup.longitude > 180.0) {
        errors.push("Invalid longitude");
    }
    if (backup.timezone === undefined ||
        backup.timezone.trim() === "" ||
        backup.timezone.length > 20) {
        errors.push("Invalid timezone");
    }
    if (!backup.offset instanceof Number ||
        backup.offset < -12 ||
        backup.offset > 12) {
        errors.push("Invalid offset");
    }
    verifyDisplay("day", backup.dayPattern, backup.dayColour, errors);
    verifyDisplay("night", backup.nightPattern, backup.nightColour, errors);
    verifyDisplay("alarm", backup.alarmPattern, backup.alarmColour, errors);

    if (errors.length > 0) {
        let errorText = "Invalid backup data:<br><ul>";
        for (const error of errors) {
            errorText += "<li>" + error + "</li>"
        }
        errorText += "</ul>"
        showNotification(errorText, "error");
        return;
    }

    // Make sure the user knows what's up.
    if (!confirm("This will replace the configuration on the device. Are you sure?")) {
        // The user chose to exit.
        return;
    }

    try {
        saveConfiguration(JSON.stringify(backup, null, 2), () => {
            hideBackup();
            loadConfiguration();
            showNotification("Backup uploaded successfully", "success");
        });
    } catch (err) {
        const msg = err.message ? err.message : JSON.stringify(err, null, 2);
        showNotification("Backup uploaded failed<br>" + msg, "error");
        return;
    }
}

/**
 * Shows the notification pop-up.
 * 
 * @param {*} text The text to show in the notification.
 * @param {*} type The type of the notification to show ("success"/"error").
 */
function showNotification(text, type) {
    let notification = document.getElementById("notification");
    
    if (timer !== undefined) {
        clearTimeout(timer);
        timer = undefined;
        notification.classList.remove("show");
    }
    notification.classList.remove("hide");

    document.getElementById("notificationText").innerHTML = text;

    if (type === "success") {
        notification.classList.add("success");
        notification.classList.remove("error");
    } else if (type === "error") {
        notification.classList.add("error");
        notification.classList.remove("success");
    }
    notification.classList.add("show");

    timer = setTimeout(function(){
        notification.classList.remove("show");
        timer = undefined;
    }, 10000);
}

/**
 * Hides the notification popup, if visible.
 */
function hideNotification() {
    notification.classList.remove("show");
    notification.classList.add("hide");
    if (timer !== undefined) {
        clearTimeout(timer);
        timer = undefined;
    }

    timer = setTimeout(function(){
        notification.classList.remove("hide");
        timer = undefined;
    }, 500);
}

function zeroPad(val, digits) {
    let v = "" + val;
    while (v.length < digits) {
        v = "0" + v;
    }

    if (v.length > digits) {
        const l = v.length;
        v = v.substring(l - digits);
    }

    return v;
}

function colourArrayToHtmlColour(jsonArray) {
    if (!Array.isArray(jsonArray) || jsonArray.length != 3) {
        return "#FFFFFF";
    }

    const r = zeroPad(jsonArray[0].toString(16), 2);
    const g = zeroPad(jsonArray[1].toString(16), 2);
    const b = zeroPad(jsonArray[2].toString(16), 2);
    return `#${r}${g}${b}`;
}

function htmlColourToColourArray(colour) {
    if (colour.length != 7) {
        return [255, 255, 255];
    }

    const r = parseInt(colour.substring(1, 3), 16);
    const g = parseInt(colour.substring(3, 5), 16);
    const b = parseInt(colour.substring(5, 7), 16);
    return [r, g, b];
}