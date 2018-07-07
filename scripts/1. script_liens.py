import os, re, requests, threading, time
from shutil import rmtree
from urllib.parse import urlencode
from pyquery import PyQuery

num_episode_regexp = re.compile('catalogue/episode/(.*?)/')
manifest_regexp = re.compile(r'''(/fr/v2/video/mnf.*?\?.+?)['"][;,]''')
widevine_regexp = re.compile(r"url: \"(.*?Widevine.*?)\",.*?headers.*?Authorization.*?value: \"(.*?)\"", re.DOTALL)

urls_ep = open('../urls_episodes.txt').read().splitlines()
headers = {'User-agent': 'Mozilla/5.0'}
cookies = {'ASP.NET_SessionId':'opipygbpcm5p14ak0tvys5je'}

if os.path.exists('../tmp'):
	try: {rmtree('../tmp'), time.sleep(0.5)}
	except: {time.sleep(1), rmtree('../tmp')}
if not os.path.exists('../tmp'):
	try: os.mkdir('../tmp')
	except: {time.sleep(1), os.mkdir('../tmp')}

s = requests.Session()
data = {'Username': 'username', 'Password': 'password', 'RememberMe': 'false'}
s.post('https://www.site.tv/fr/v2/account/login', headers=headers, data=data, cookies=cookies)

def get_info_video(url):
	nom_video = url.split('/')[-1]
	
	r1 = s.get(url, headers=headers, cookies=cookies)
	s.get('http://www.site.tv/fr/v2/video/advertisement?idepisode='+num_episode_regexp.findall(url)[0], headers=headers, cookies=cookies)
	
	url_mnff = 'http://www.site.tv'+manifest_regexp.findall(r1.text)[0]
	r2 = s.get(url_mnff, headers=headers, cookies=cookies)

	url_widevine = widevine_regexp.findall(r1.text)[0][0]
	auth_token = widevine_regexp.findall(r1.text)[0][1]

	if not os.path.exists('../tmp/_info'):  os.mkdir('../tmp/_info')
	if not os.path.exists('../tmp/_info/'+nom_video):  os.mkdir('../tmp/_info/'+nom_video)

	# https://github.com/peak3d/inputstream.adaptive/wiki/Playing-multi-bitrate-stream-from-a-python-addon
	with open('../tmp/_info/'+nom_video+'/licence_key.txt', 'w') as file:
		file.write(url_widevine + "|" + urlencode({"Authorization": auth_token}) + "&User-Agent=Mozilla%2F5.0|R{SSM}|")

	with open('../tmp/_info/'+nom_video+'/manifest.mpd', 'w') as file:
		file.write(r2.text)
		
	d = PyQuery(r2.content, parser='html')

	urls = []
	for i in range(len(d('AdaptationSet'))):
		time = [0]
		
		bandwidth = max(d('AdaptationSet').eq(i)('Representation').map(lambda ind,elem: int(PyQuery(elem).attr('bandwidth'))))
		segments = d('AdaptationSet').eq(i)('SegmentTemplate')('SegmentTimeline')('S')
		
		for j in range(len(segments)):
			duration = int(segments.eq(j).attr('d'))
			
			if segments.eq(j).attr('r'):
				repetitions = int(segments.eq(j).attr('r')) + 1
			else:
				repetitions = 1
		
			for _ in range(repetitions):
				time.append(time[-1] + duration)
			
		del time[-1]
		
		urls.append(d('AdaptationSet').eq(i)('SegmentTemplate').attr('initialization').replace('$Bandwidth$', str(bandwidth)))
		url_media = d('AdaptationSet').eq(i)('SegmentTemplate').attr('media').replace('$Bandwidth$', str(bandwidth))
		for j in range(len(time)):
			urls.append(url_media.replace('$Time$', str(time[j])))

	with open('../tmp/_info/'+nom_video+'/url_fragments.txt','w') as f:
		for i in range(len(urls)):
			f.write(urls[i]+'\n')


threads = []
for i in range(len(urls_ep)):
	thread = threading.Thread(target=get_info_video, name=urls_ep[i].split('/')[-1], args=(urls_ep[i],))
	thread.start()
	threads.append(thread)

for t in threads:  t.join()   # Attente fin requetes
