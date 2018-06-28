from __future__ import unicode_literals
import os, re, requests, threading, queue, time

position_regexp = re.compile("=(.+?),format")
s = requests.Session()

def get_url(nom_video,url):
	r = s.get(url,headers = {'User-agent': 'Mozilla/4.0'})
	
	pos_str = position_regexp.findall(url)[0]
	if 'i' in pos_str : pos = -1
	else : pos = int(pos_str)

	if 'AAC' in url or 'aac_' in url :  type = 'audio'
	elif 'video' in url :  type = 'video'
	
	files[nom_video].append([type, pos, r.content])

class Worker(threading.Thread):
	def __init__(self,id):
		threading.Thread.__init__(self)
		self.id = id
		self.url_number = 0
	def run(self):
		while True:
			[nom_video,url,url_number] = q.get()
			get_url(nom_video,url)
			self.url_number = url_number
			q.task_done()

files = {}

max_threads = 50
vids = os.listdir('../tmp/_info')

L = []
for nom_video in vids:
	files[nom_video] = []
	with open('../tmp/_info/'+nom_video+'/url_fragments.txt') as file:
		L.append([nom_video, file.read().splitlines()])

q = queue.Queue()
threads = []
for i in range(max_threads):
	t = Worker(i)
	t.daemon = True
	t.start()
	threads.append(t)

url_number = 0
for info_vid in L:
	for url in info_vid[1]:
		url_number +=1
		q.put([info_vid[0],url,url_number])

nb_urls = url_number
while q.qsize()>0:   # Toujours laisser ce while avant q.join() pour garder arret avec CTRL+C (sauf a la fin quand q est vide)
	os.system('cls')
	for t in threads:
		print('Thread %s: %s/%s' %(t.id,t.url_number,nb_urls))
	time.sleep(1)

print('\nAlmost finished ...')
q.join()

if not os.path.exists('../tmp/_data'):  os.mkdir('../tmp/_data')
for nom_video in files.keys(): 
	files[nom_video].sort()
	f_audio = open('../tmp/_data/'+nom_video+'_audio','wb')
	f_video = open('../tmp/_data/'+nom_video+'_video','wb')
	for i in range(len(files[nom_video])):
		if files[nom_video][i][0]=='audio':  f_audio.write(files[nom_video][i][2])
		if files[nom_video][i][0]=='video':  f_video.write(files[nom_video][i][2])
	f_audio.close(); f_video.close()

print('Completed !')
input()
