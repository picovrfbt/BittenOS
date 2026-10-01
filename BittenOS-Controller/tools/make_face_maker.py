"""Build tools/face-maker.html from data/animator.html.

The face maker is the Animator with no mask behind it: the same editor, told by a
data-standalone attribute to use this mask's layout baked into the page, offer a
layout editor, and save as a download instead of to the mask. Generated rather
than kept by hand so the two can never drift apart. Run it after changing
data/animator.html:

    python tools/make_face_maker.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), 'data', 'animator.html')
DST = os.path.join(HERE, 'face-maker.html')

html = open(SRC, encoding='utf-8').read()
if '<html lang="en">' not in html:
    raise SystemExit('data/animator.html has no <html lang="en"> tag to mark')
html = html.replace('<html lang="en">', '<html lang="en" data-standalone>', 1)
html = html.replace('<title>BittenOS Animator</title>', '<title>Face maker</title>', 1)
# after the doctype, so nothing comes before it
banner = ('<!-- Generated from data/animator.html by tools/make_face_maker.py. '
          'Edit that file and run the script rather than changing this one. -->\n')
html = html.replace('<!DOCTYPE html>\n', '<!DOCTYPE html>\n' + banner, 1)
with open(DST, 'w', encoding='utf-8', newline='\n') as f:
    f.write(html)
print('Wrote', os.path.relpath(DST, os.path.dirname(HERE)))
