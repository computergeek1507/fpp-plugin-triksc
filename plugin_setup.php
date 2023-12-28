<?
function returnIfExists($json, $setting) {
    if ($json == null) {
        return "";
    }
    if (array_key_exists($setting, $json)) {
        return $json[$setting];
    }
    return "";
}

function convertAndGetSettings() {
    global $settings;
        
    $cfgFile = $settings['configDirectory'] . "/plugin.triksc.json";
    if (file_exists($cfgFile)) {
        $j = file_get_contents($cfgFile);
        $json = json_decode($j, true);
        return $json;
    }
    $j = "{\"port\":\"\",\"speed\":115200,\"startchannel\":1,\"width\":1,\"height\":1}";
    return json_decode($j, true);
}

$pluginJson = convertAndGetSettings();
?>


<div id="global" class="settings">
<fieldset>
<legend>FPP triksc Config</legend>

<script>

var serialEventConfig = <? echo json_encode($pluginJson, JSON_PRETTY_PRINT); ?>;


var uniqueId = 1;
var modelOptions = "";



function SaveSerialEventItems() {

    newserialeventConfig = { "port": '', "speed": 115200, "startchannel": 1, "width": 1, "height": 1};
    
    newserialeventConfig["port"] = document.getElementById("serialport").value;
    newserialeventConfig["speed"] = document.getElementById("serialspeed").value;
    newserialeventConfig["startchannel"] = document.getElementById("startchannel").value;
    newserialeventConfig["width"] = document.getElementById("panelwidth").value;
    newserialeventConfig["height"] = document.getElementById("panelheight").value;

    var data = JSON.stringify(newserialeventConfig);
    $.ajax({
        type: "POST",
	url: 'api/configfile/pplugin.triksc.json',
        dataType: 'json',
        async: false,
        data: data,
        processData: false,
        contentType: 'application/json',
        success: function (data) {
           SetRestartFlag(2);
        }
    });
}


$(document).ready(function() {
                  



});

</script>

<div>
Serial Port:<input type='text' id='serialport' minlength='7' maxlength='30' size='15' class='serialport' />
Speed:<input type='number' id='serialspeed' class='serialspeed' />
Start Channel:<input type='number' id='startchannel' class='startchannel' />
Height:<input type='number' id='panelheight' class='panelheight' min="1" max="4"/>
Width:<input type='number' id='panelwidth' class='panelwidth' min="1" max="4"/>
<div>
<table border=0>
<tr><td colspan='2'>
        <input type="button" value="Save" class="buttons genericButton" onclick="SaveSerialEventItems();">
    </td>
</tr>
</table>

</div>
<div>
<p>

<p>
</div>
</div>
<script>


document.getElementById("serialport").value = serialEventConfig["port"];
document.getElementById("serialspeed").value = serialEventConfig["speed"];
document.getElementById("startchannel").value = serialEventConfig["startchannel"];
document.getElementById("panelwidth").value = serialEventConfig["width"];
document.getElementById("panelheight").value = serialEventConfig["height"];

</script>

</fieldset>
</div>
