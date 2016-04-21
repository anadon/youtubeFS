#!/usr/bin/python

import httplib2
import os
import sys

import youtube_dl
from apiclient.discovery import build
from oauth2client.client import flow_from_clientsecrets
from oauth2client.file import Storage
from oauth2client.tools import argparser, run_flow
from subprocess import Popen, PIPE

CLIENT_SECRETS_FILE = ".client_secrets.json"

YOUTUBE_READONLY_SCOPE = "https://www.googleapis.com/auth/youtube.readonly"
YOUTUBE_API_SERVICE_NAME = "youtube"
YOUTUBE_API_VERSION = "v3"

flow = flow_from_clientsecrets(CLIENT_SECRETS_FILE, scope=YOUTUBE_READONLY_SCOPE)

storage = Storage("%s-oath2.json" % sys.argv[0])
credentials = storage.get()

if credentials is None or credentials.invalid:
    flags = argparser.parse_args()
    credentials = run_flow(flow, storage, flags)

youtube = build(YOUTUBE_API_SERVICE_NAME, YOUTUBE_API_VERSION, http=credentials.authorize(httplib2.Http()))

channels_response = youtube.channels().list(mine=True, part="contentDetails").execute()

i = 1
ids = list()
titles = list()

for channel in channels_response["items"]:
    uploads_list_id = channel["contentDetails"]["relatedPlaylists"]["uploads"]

    playlistitems_list_request = youtube.playlistItems().list(playlistId=uploads_list_id, part="snippet", maxResults=50)

    while playlistitems_list_request:
        playlistitems_list_response = playlistitems_list_request.execute()

        for playlist_item in playlistitems_list_response["items"]:
            title = playlist_item["snippet"]["title"]
            video_id = playlist_item["snippet"]["resourceId"]["videoId"]
            print("%d:\t%s (%s)" % (i, title, video_id))
            ids.append(video_id)
            titles.append(title)
            i += 1

        playlistitems_list_request = youtube.playlistItems().list_next(playlistitems_list_request, channels_response)

print("Please select a video by number: ")
selected = int(input()) - 1

ydl_opts = {'format': 'mp4',
            'ratelimit': 1000000,
            'outtmpl': '%(id)s.%(ext)s'}
with youtube_dl.YoutubeDL(ydl_opts) as ydl:
    ydl.download([ids[selected]])

decout = Popen(["./decode", ids[selected] + ".mp4"], stdout=PIPE)
decout = decout.communicate()[0]
outfile = open(titles[selected], "wb")
outfile.write(decout)
outfile.flush()
outfile.close()

os.remove(ids[selected] + ".mp4")
