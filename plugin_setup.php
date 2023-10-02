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
    $j = "{\"port\":\"\",\"speed\":115200,\"serialEvents\":[]}";
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
function AddSerialEventItem(type, modType ) {
    var id = $("#serialeventTableBody > tr").length + 1;
    var html = "<tr class='fppTableRow";
    if (id % 2 != 0) {
        html += " oddRow'";
    }
    html += "'><td class='colNumber rowNumber'>" + id + ".</td><td><span style='display: none;' class='uniqueId'>" + uniqueId + "</span></td>";

    //html += DeviceSelect(SerialDevices, "");
    
    html += "<td><input type='text' minlength='7' maxlength='15' size='15' class='desc' /></td>";
    html += "<td><select class='conditiontype'>";
    html += "<option value='contains'";
    if(type == 'contains') {html += " selected ";}
    html += ">Contains</option><option value='startswith'";
    if(type == 'startswith') {html += " selected ";}
    html += ">Starts With</option><option value='endswith'";
    if(type == 'endswith') {html += " selected ";}
    html += ">Ends With</option><option value='regex'";
    if(type == 'regex') {html += " selected ";}
    html += ">Regex</option></select></td>";
    html += "<td><input type='text'  minlength='7' maxlength='15' size='15' class='conditionvalue' /></td>";
    
    html += "<td><select class='type'>";
    html += "<option value='none'";
    if(modType == 'none') {html += " selected ";}
    html += ">None</option><option value='substring'";
    if(modType == 'substring') {html += " selected ";}
    html += ">SubString</option><option value='regex'";
    if(modType == 'regex') {html += " selected ";}
    html += ">Regex</option></select></td>";

    html += "<td><input type='text'  minlength='7' maxlength='15' size='15' class='modifiervalue' /></td>";
    html += "<td><table class='fppTable' border=0 id='tableSerialCommand_" + uniqueId +"'></td>";
    html += "<td>Command:</td><td><select class='serialcommand' id='serialcommand" + uniqueId + "' onChange='CommandSelectChanged(\"serialcommand" + uniqueId + "\", \"tableSerialCommand_" + uniqueId + "\" , false, PrintArgsInputsForEditable);'><option value=''></option></select></td></tr>";
    html += "</table></td></tr>";
    //selected
    $("#serialeventTableBody").append(html);

    LoadCommandList($('#serialcommand' + uniqueId));

    newRow = $('#serialeventTableBody > tr').last();
    $('#serialeventTableBody > tr').removeClass('selectedEntry');
    DisableButtonClass('deleteEventButton');
    uniqueId++;

    return newRow;
}

function SaveSerialEventItem(row) {
    var id = $(row).find('.uniqueId').html();
    var desc = $(row).find('.desc').val();
	var conditiontype = $(row).find('.conditiontype').val();
    var conditionvalue = $(row).find('.conditionvalue').val();
    var modifiertype = $(row).find('.modifiertype').val();
    var modifiervalue = $(row).find('.modifiervalue').val();

    var json = {
        "description": desc,
        "condition": conditiontype,
        "conditionValue": conditionvalue,
        "modifier": modifiertype,
        "modifierValue": modifiervalue
    };

    CommandToJSON('serialcommand' + id, 'tableSerialCommand_' + id, json, true);
    return json;
}

function SaveSerialEventItems() {

    newserialeventConfig = { "port": '', "speed": 115200, "serialEvents": []};
    var i = 0;
    $("#serialeventTableBody > tr").each(function() {
        newserialeventConfig["serialEvents"][i++] = SaveSerialEventItem(this);
    });
    
    newserialeventConfig["port"] = document.getElementById("serialport").value;
    newserialeventConfig["speed"] = document.getElementById("serialport").serialspeed;

    var data = JSON.stringify(newserialeventConfig);
    $.ajax({
        type: "POST",
	url: 'api/configfile/plugin.serial-event.json',
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

function RefreshLastMessages() {
    $.get('api/plugin-apis/SERIALEVENT/list', function (data) {
          $("#lastMessages").text(data);
        }
    );
}


function RenumberRows() {
    var id = 1;
    $('#serialeventTableBody > tr').each(function() {
        $(this).find('.rowNumber').html('' + id++ + '.');
        $(this).removeClass('oddRow');

        if (id % 2 != 0) {
            $(this).addClass('oddRow');
        }
    });
}
function RemoveSerialEventItem() {
    if ($('#serialeventTableBody').find('.selectedEntry').length) {
        $('#serialeventTableBody').find('.selectedEntry').remove();
        RenumberRows();
    }
    DisableButtonClass('deleteEventButton');
}


$(document).ready(function() {
                  
    $('#serialeventTableBody').sortable({
        update: function(event, ui) {
            RenumberRows();
        },
        item: '> tr',
        scroll: true
    }).disableSelection();

    $('#serialeventTableBody').on('mousedown', 'tr', function(event,ui){
        $('#serialeventTableBody tr').removeClass('selectedEntry');
        $(this).addClass('selectedEntry');
        EnableButtonClass('deleteEventButton');
    });
});

</script>

<div>
Serial Port:<input type='text' id='serialport' minlength='7' maxlength='30' size='15' class='serialport' />
Speed:<input type='number' id='serialspeed' class='serialspeed' />
<div>
<table border=0>
<tr><td colspan='2'>
        <input type="button" value="Save" class="buttons genericButton" onclick="SaveSerialEventItems();">
        <input type="button" value="Add" class="buttons genericButton" onclick="AddSerialEventItem('contains', 'none');">
        <input id="delButton" type="button" value="Delete" class="deleteEventButton disableButtons genericButton" onclick="RemoveSerialEventItem();">
    </td>
</tr>
</table>

<div class='fppTableWrapper fppTableWrapperAsTable'>
<div class='fppTableContents'>
<table class="fppSelectableRowTable" id="serialeventTable"  width='100%'>
<thead><tr class="fppTableHeader"><th>#</th><th></th><th>Description</th><th>Condition Type</th><th>Condition Value</th><th>Modifier Type</th><th>Modifier Value</th><th>Command</th></tr></thead>
<tbody id='serialeventTableBody'>
</tbody>
</table>
</div>

</div>
<div>
<p>
<div class="col-auto">
        <div>
            <div class="row">
                <div class="col">
                    Last Messages:&nbsp;<input type="button" value="Refresh" class="buttons" onclick="RefreshLastMessages();">
                </div>
            </div>
            <div class="row">
                <div class="col">
                    <pre id="lastMessages" style='min-width:150px; margin:1px;min-height:300px;'></pre>
                </div>
            </div>
        </div>
    </div>
<p>
</div>
</div>
<script>

$.each(serialEventConfig["serialEvents"], function( key, val ) {
    var row = AddSerialEventItem(val["condition"], val["modifier"]);
    $(row).find('.desc').val(val["description"]);
    $(row).find('.conditionvalue').val(val["conditionValue"]);
    $(row).find('.modifiervalue').val(val["modifierValue"]);

    var id = parseInt($(row).find('.uniqueId').html());
    PopulateExistingCommand(val, 'serialcommand' + id, 'tableSerialCommand_' + id, false, PrintArgsInputsForEditable);
});

document.getElementById("serialport").value = serialEventConfig["port"];
document.getElementById("serialspeed").value = serialEventConfig["speed"];


</script>

</fieldset>
</div>
