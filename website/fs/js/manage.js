function create_connection_websocket() {

  var socket = new WebSocket("ws://"+window.location.hostname+":8765");

  socket.onopen = function(e) {
    console.log("Соединение установлено");
  };
  socket.onmessage = function(event) {

  };
  socket.onclose = function(event) {
    console.log('Соединение прервано');
  };
  socket.onerror = function(error) {
    console.log(`[error] ${error.message}`);
  };
  return socket;
}


$(document).ready(function() {
  websocket = create_connection_websocket();
});
