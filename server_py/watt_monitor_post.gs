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
    
    var maxRows = 4320;  // 3 days of 1-minute data (1440 × 3)
    
    // === Automatically set header (only if row 1 is empty) ===
    var headerCheck = sheet.getRange(1, 1).getValue();
    if (headerCheck === "") {
      var names = params.names || [];
      var header = ["time", ...names];  // First column: "time", followed by channel names and "total"
      sheet.getRange(1, 1, 1, header.length).setValues([header]);
    }
    
    var time = params.time;
    var powers = params.power || [];
    var newRecord = [time, ...powers];  // New data row: time + power values
    
    var lastRow = sheet.getLastRow();
    var targetRow;
    
    // FIFO shift: copy rows 2 to (maxRows) → rows 3 to maxRows+1 (preserves header)
    var numCols = newRecord.length;
    sheet.getRange(2, 1, maxRows-1, numCols).copyTo(sheet.getRange(3, 1));
    targetRow = 2;  // Write to the top row below the header
    
    // Write the new record
    sheet.getRange(targetRow, 1, 1, newRecord.length).setValues([newRecord]);
    
    return ContentService.createTextOutput(JSON.stringify({response: "ok"})).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    Logger.log(error.toString());
    return ContentService.createTextOutput(JSON.stringify({error: error.toString()}))
           .setMimeType(ContentService.MimeType.JSON);
  }
}