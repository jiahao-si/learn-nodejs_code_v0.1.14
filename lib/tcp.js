/**
 * 这个作为 server ，可以监听端口等
 * @param {*} on_connection
 * @param {*} options
 * @returns
 */
exports.createServer = function (on_connection, options) {
  var server = new node.tcp.Server();
  /**
   * 证明了 server 本质是事件驱动，可以监听各种事件
   */
  server.addListener('connection', on_connection);
  //server.setOptions(options);
  return server;
};

/**
 * 作为 client， 连接 port 和 host
 * @param {*} port
 * @param {*} host
 * @returns
 */
exports.createConnection = function (port, host) {
  var connection = new node.tcp.Connection();
  connection.connect(port, host);
  return connection;
};
