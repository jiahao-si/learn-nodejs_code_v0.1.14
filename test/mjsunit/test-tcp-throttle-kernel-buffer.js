node.mixin(require("common.js"));
tcp = require("/tcp.js");
PORT = 20444;
N = 30*1024; // 500kb

puts("build big string");
var body = "";
for (var i = 0; i < N; i++) {
  body += "C";
}

puts("start server on port " + PORT);

server = tcp.createServer(function (connection) {
  connection.addListener("connect", function () {
    connection.send(body);
    connection.close();
  });
});
server.listen(PORT);


chars_recved = 0;
npauses = 0;


var paused = false;
client = tcp.createConnection(PORT);
client.setEncoding("ascii");
client.addListener("receive", function (d) {
  chars_recved += d.length;
  puts("got " + chars_recved);
  if (!paused) {
    client.readPause();
    npauses += 1;
    paused = true;
    puts("pause");
    x = chars_recved;
    setTimeout(function () {
      assertEquals(chars_recved, x);
      client.readResume();
      puts("resume");
      paused = false;
    }, 100);
  }
});

client.addListener("eof", function () {
  server.close();
  client.close();
});

process.addListener("exit", function () {
  assertEquals(N, chars_recved);
  assertTrue(npauses > 2);
});
