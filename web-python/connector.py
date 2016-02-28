import web
import mdc_api
import json
import os
import codecs
import pprint
import time
import base64
from threading import Timer
from pybars import Compiler

# map URL to class to handle requests
urls = (
    '/', 'index',
    '/knock-sensor/(.*)', 'knocksensor',
    '/api/knock-sensor/(.*)', 'apiknocksensor',
    '/notification', 'notification'
)

token = os.environ['TOKEN'] # Get from connector.mbed.com
if token == '':
    raise Exception('Missing token, run \'TOKEN=xxx python connector.py\'')
connector = mdc_api.connector(token, 'https://ds-test-sl.dev.mbed.com')
# connector.debug(True)
compiler = Compiler() # pybars compiler

KNOCK_RESOURCE = '/accelerometer/0/last_knock'

class index:
    def GET(self):
        template = None
        with codecs.open('views/endpoints.html', encoding='utf-8', mode='r') as template_file:
            template = compiler.compile(template_file.read())

        e = connector.getEndpoints()
        while not e.isDone():
            None
        if e.error:
            raise Exception(e.error.errType)

        return template({'endpoints': e.result})

class notification:
    # handle asynchronous events
    def PUT(self):
        if web.data: # verify there is data to process
            print json.loads(web.data()).keys()
            connector.handler(web.data()) # hand the data to the connector handler
        return web.ok


class knocksensor:
    def GET(self, id):
        with codecs.open('views/knock-sensor.html', encoding='utf-8', mode='r') as template_file:
            template = compiler.compile(template_file.read())

        e = connector.getResourceValue(id, KNOCK_RESOURCE)
        while not e.isDone():
            time.sleep(1)
        if e.error:
            raise Exception(e.error.errType)

        # also put a subscription for the resource in...
        r = connector.putResourceSubscription(id, KNOCK_RESOURCE)
        while not r.isDone():
            None
        if r.error:
            raise Exception(r.error.errType)

        return template({'value': e.result, 'id': id})

# because I don't know how to do websockets in Python
class apiknocksensor:
    def GET(self, id):
        e = connector.getResourceValue(id, KNOCK_RESOURCE)
        while not e.isDone():
            None
        if e.error:
            raise Exception(e.error.errType)
        return e.result

# 'notifications' are routed here
def notificationHandler(data):
    # if you implement web sockets, you should use this :-) see the node example
    print "Notification Data Received: %s" %data['notifications']
    print "Notification Payload: %s" % base64.b64decode(data['notifications'][0]['payload'])

def registerNotification():
    p = connector.putCallback('http://' + os.environ['C9_HOSTNAME'] + '/notification')
    while not p.isDone():
        None
    if p.error:
        raise Exception(p.error.errType)
    print "Callback URL is %s" % 'http://' + os.environ['C9_HOSTNAME'] + '/notification'

if __name__ == "__main__":
    connector.setHandler('notifications', notificationHandler) # send 'notifications' to the notificationHandler FN

    # 2s after webpy starts we register notification
    t = Timer(2, registerNotification)
    t.start()

    app = web.application(urls, globals())
    app.run()
