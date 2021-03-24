CMD_UPDATE_DEVICE_STATUS = 1

DEVICE_STATUS = {
  "irp" : [],
  "afu" : [] ,
  "lit_en" : [],
  "emit" : 0,
  "mode" : 0,
  "submode" : 0,
  "mode_vz" : 0
}

function updateDeviceStatus(arr) {
  for (let i = 0; i < 4; ++i) {
    DEVICE_STATUS.irp[i] = arr.getUint8(i, true);
    DEVICE_STATUS.afu[i] = arr.getUint8(i+4, true);
    DEVICE_STATUS.lit_en[i] = arr.getUint8(i+8, true);
  }
  DEVICE_STATUS.emit = arr.getUint8(12, true);
  DEVICE_STATUS.mode = arr.getUint8(13, true);
  DEVICE_STATUS.submode = arr.getUint8(14, true);
  DEVICE_STATUS.mode_vz = arr.getUint8(15, true);
  console.log(DEVICE_STATUS);
}

function getSysPkgFromArray(arr) {
  return {
    "SYNQSEQ":arr.getUint32(0, true),
    "cmd":arr.getUint16(4, true),
    "src_id":arr.getUint16(6, true),
    "dest_id":arr.getUint16(8, true),
    "misc":arr.getUint8(9, true),
    "pack_cnt":arr.getUint8(10, true),
    "byte_cnt":arr.getUint16(11, true),
    "crc16":arr.getUint16(13, true)
  }
}

function setSysPkgToArray(pkg) {
  let arr = new ArrayBuffer(16);
  let view = new DataView(arr);
  view.setUint32(0, pkg.SYNQSEQ, true);
  view.setUint16(4, pkg.cmd, true);
  view.setUint16(6, pkg.src_id, true);
  view.setUint16(8, pkg.dest_id, true);
  view.setUint8(9, pkg.misc, true);
  view.setUint8(10, pkg.pack_cnt, true);
  view.setUint16(11, pkg.byte_cnt, true);
  view.setUint16(13, pkg.crc16, true);

  return arr;
}

ANSWER = 1;
websocket = null;

function ws_parse(arrBuf) {
  let buf = new DataView(arrBuf);
  let pkg = getSysPkgFromArray(buf);
  console.log(pkg);

  switch(pkg.cmd) {
    case CMD_UPDATE_DEVICE_STATUS:
      let payload = new DataView(arrBuf, 16);
      updateDeviceStatus(payload);
      break;
  }



  // console.log(DEVICE_STATUS);

  // DEVICE_STATUS.mode_vz = ANSWER;
  // ANSWER = (ANSWER > 255) ? (0):(ANSWER+1);
  // let arr = ObjToArr();
  // // websocket.binaryType = 'arraybuffer';
  // websocket.send(arr);

}

function create_connection_websocket() {

  var socket = new WebSocket("ws://"+window.location.hostname+":8765");

  socket.onopen = function(e) {
    console.log("Соединение установлено");
  };
  socket.onmessage = function(event) {
    var blob = event.data;
    blob.arrayBuffer().then(buffer => ws_parse(buffer));
  };
  socket.onclose = function(event) {
    console.log('Соединение прервано');
  };
  socket.onerror = function(error) {
    console.log(`[error] ${error.message}`);
  };
  return socket;
}


function ObjToArr() {
  let arr = new ArrayBuffer(16);
  let buffer = new Uint8Array(arr);

  for (let i = 0; i < DEVICE_STATUS.irp.length; ++i) {
    buffer[i] = DEVICE_STATUS.irp[i];
    buffer[i+4] = DEVICE_STATUS.afu[i];
    buffer[i+8] = DEVICE_STATUS.lit_en[i];
  }
  buffer[12] = DEVICE_STATUS.emit;
  buffer[13] = DEVICE_STATUS.mode;
  buffer[14] = DEVICE_STATUS.submode;
  buffer[15] = DEVICE_STATUS.mode_vz;

  // var blob = new Blob([buffer], {type:"application/x-binary"})
  return arr;
}


$(document).ready(function() {
  websocket = create_connection_websocket();

  // if (FLAG_ANSWER == 1) {
  // }
});
