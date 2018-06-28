import os, sys

data_size      = sum(map(lambda x: x.stat().st_size, os.scandir('../tmp/_data')))
decrypted_size = sum(map(lambda x: x.stat().st_size, os.scandir('../tmp/_decrypted')))

if data_size != decrypted_size:
	print('Error, size mismatch')
	sys.exit(0)
else:
	print('OK!')

L = os.listdir('../tmp/_info')
for i in range(len(L)):
	os.system('start ffmpeg -i "%s" -i "%s" -c copy -map 0 -map 1 "%s"'
				%('../tmp/_decrypted/'+L[i]+'_video.ts','../tmp/_decrypted/'+L[i]+'_audio.m4a','../'+L[i]+'.mp4'))
