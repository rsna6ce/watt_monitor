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
    
    if (lastRow >= maxRows + 1) {  // Header (1 row) + max data rows
      // FIFO shift: copy rows 3 to (maxRows+1) → rows 2 to maxRows (preserves header)
      var numCols = newRecord.length;
      sheet.getRange(3, 1, maxRows, numCols).copyTo(sheet.getRange(2, 1));
      targetRow = maxRows + 1;  // Write to the bottom row below the header
    } else {
      targetRow = lastRow + 1;  // Append to next empty row (row 2 on first run)
    }
    
    // Write the new record
    sheet.getRange(targetRow, 1, 1, newRecord.length).setValues([newRecord]);
    
    return ContentService.createTextOutput(JSON.stringify({response: "ok"})).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    Logger.log(error.toString());
    return ContentService.createTextOutput(JSON.stringify({error: error.toString()}))
           .setMimeType(ContentService.MimeType.JSON);
  }
}