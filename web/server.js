var MbedConnector = require('mbed-connector');
var EventEmitter = require('events');
var app = require('express')();
var server = require('http').Server(app);
var io = require('socket.io')(server);
var bodyParser = require('body-parser');

const KNOCK_RESOURCE = '/accelerometer/0/last_knock';

var connector = new MbedConnector({
  accessKey: process.env.TOKEN,
  host: 'https://ds-test-sl.dev.mbed.com'
});

app.set('view engine', 'html');
app.engine('html', require('hbs').__express);
app.use(bodyParser.json());

// Get all our endpoints
app.get('/', function(req, res, next) {
  connector.getEndpoints(function(err, endpoints) {
    if (err) return next(err);

    res.render('endpoints.html', { endpoints: endpoints });
  });
});

// The knock page
app.get('/knock-sensor/:id', function(req, res, next) {
  connector.getResourceValue(req.params.id, KNOCK_RESOURCE, function(err, value) {
    if (err) return next(err);

    res.render('knock-sensor.html', { value: value, id: req.params.id });
  });
});

app.put('/notification', function(req, res, next) {
  connector.handleNotifications(req.body);
  res.send('OK');
});

// Notifications are sent over a web socket
var notifications = new EventEmitter();
connector.on('notifications', function(data) {
  data.forEach(function(n) {
    notifications.emit(n.ep + '/' + n.path, n.payload);
  });
});

io.on('connection', function(socket) {
  socket.on('subscribe-knocks', function(id) {
    connector.putResourceSubscription(id, KNOCK_RESOURCE, function() {});
    notifications.on(id + '/' + KNOCK_RESOURCE, function(data) {
      socket.emit('knock', data);
    });
    socket.on('disconnect', function() {
      connector.deleteResourceSubscription(id, KNOCK_RESOURCE, function() {});
    });
  });
});


server.listen(process.env.PORT || 8210, function() {
  console.log('Listening on port', process.env.PORT || 8210);

  var n = 'http://' + process.env.C9_HOSTNAME + '/notification';
  console.log(n);

  connector.putCallback({ url: n }, function(err) {
    if (err) return console.error(err);

    console.log('Set callback URL to http://' + process.env.C9_HOSTNAME);
  });
});
