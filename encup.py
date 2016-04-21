#!/usr/bin/python
import os

from googleapiclient.errors import HttpError
from oauth2client.tools import argparser
from subprocess import Popen, PIPE

from upload import get_authenticated_service, initialize_upload

VALID_PRIVACY_STATUSES = ("public", "private", "unlisted")

if __name__ == '__main__':
    argparser.add_argument("--file", required=True, help="File to encode and upload")
    argparser.add_argument("--description", help="Video description",
                           default="youtubeFS upload -- https://github.com/anadon/youtubeFS")
    argparser.add_argument("--privacyStatus", choices=VALID_PRIVACY_STATUSES,
                           default=VALID_PRIVACY_STATUSES[2], help="Video privacy status.")
    args = argparser.parse_args()

    if not os.path.exists(args.file):
        exit("Please specify a valid file using the --file= parameter.")

    outpath = "./" + args.file + ".mp4"

    encout = Popen(["./encode", args.file], stdout=PIPE)
    encout = encout.communicate()[0]
    outfile = open(outpath, "wb")
    outfile.write(encout)
    outfile.flush()
    outfile.close()

    args.keywords = ""
    args.category = "22"
    args.title = args.file
    args.file = outpath

    youtube = get_authenticated_service(args)
    try:
        initialize_upload(youtube, args)
    except HttpError as e:
        print("An HTTP error %d occurred:\n%s" % (e.resp.status, e.content))

    os.remove(outpath)
