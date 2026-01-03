function doPost(e) {
  try {
    var params = JSON.parse(e.postData.contents);
    
    if (params.function !== "watt_monitor_v1") {
      return ContentService.createTextOutput(JSON.stringify({error: "Invalid function"})).setMimeType(ContentService.MimeType.JSON);
    }
    
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName("data");
    if (!sheet) {
      return ContentService.createTextOutput(JSON.stringify({error: "Sheet not found"})).setMimeType(ContentService.MimeType.JSON);
    }
    
    var maxRows = 4320;
    
    // === ヘッダ自動設定（1行目が空の場合のみ）===
    var headerCheck = sheet.getRange(1, 1).getValue();
    if (headerCheck === "") {
      var names = params.names || [];
      var header = ["time", ...names];
      sheet.getRange(1, 1, 1, header.length).setValues([header]);
    }
    
    var time = params.time;
    var powers = params.power || [];
    var newRecord = [time, ...powers];
    
    var lastRow = sheet.getLastRow();
    var targetRow;
    
    if (lastRow >= maxRows + 1) {  // ヘッダ1行 + データmaxRows行
      // FIFOシフト（2行目～maxRows+1行目を1行目～maxRows行目にコピー → ヘッダは守る）
      var numCols = newRecord.length;
      sheet.getRange(3, 1, maxRows, numCols).copyTo(sheet.getRange(2, 1));
      targetRow = maxRows + 1;  // ヘッダの下の最下行
    } else {
      targetRow = lastRow + 1;  // 次の空行（初回は2行目）
    }
    
    sheet.getRange(targetRow, 1, 1, newRecord.length).setValues([newRecord]);
    
    return ContentService.createTextOutput(JSON.stringify({response: "ok"})).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    Logger.log(error.toString());
    return ContentService.createTextOutput(JSON.stringify({error: error.toString()}))
           .setMimeType(ContentService.MimeType.JSON);
  }
}
