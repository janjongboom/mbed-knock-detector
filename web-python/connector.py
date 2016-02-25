import web
import mdc_api
import json
import os
import codecs
import pprint
import time
import base64
from pybars import Compiler

# map URL to class to handle requests
urls = (
    '/', 'index',
    '/knock-sensor/(.*)', 'knocksensor',
    '/api/knock-sensor/(.*)', 'apiknocksensor',
)

token = os.environ['TOKEN'] # Get from connector.mbed.com
if token == '':
    raise Error('Missing token, run \'TOKEN=xxx python connector.py\'')
connector = mdc_api.connector(token)
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
            raise Error(e.error)

        return template({'endpoints': e.result})

class knocksensor:
    def GET(self, id):
        with codecs.open('views/knock-sensor.html', encoding='utf-8', mode='r') as template_file:
            template = compiler.compile(template_file.read())

        e = connector.getResourceValue(id, KNOCK_RESOURCE)
        while not e.isDone():
            time.sleep(1)
        if e.error:
            raise Error(e.error)

        return template({'value': e.result, 'id': id})

# because I don't know how to do websockets in Python
class apiknocksensor:
    def GET(self, id):
        e = connector.getResourceValue(id, KNOCK_RESOURCE)
        while not e.isDone():
            None
        if e.error:
            raise Error(e.error)
        return e.result

# 'notifications' are routed here
def notificationHandler(data):
    print "Notification Data Received: %s" %data['notifications']
    print "Notification Payload: %s" % base64.b64decode(data['notifications'][0]['payload'])

if __name__ == "__main__":
    e = connector.startLongPolling()
    connector.setHandler('notifications', notificationHandler) # send 'notifications' to the notificationHandler FN
    bla = connector.putResourceSubscription('38e7a161-1932-4da1-a76e-39e9999ad258', KNOCK_RESOURCE)
    while not bla.isDone():
        None
    if bla.error:
        raise Error(bla.error)
    app = web.application(urls, globals())
    app.run()
