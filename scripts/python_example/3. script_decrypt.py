import os, time

if not os.path.exists('../tmp/_decrypted'):  os.mkdir('../tmp/_decrypted')
L = os.listdir('../tmp/_data')
for i in range(len(L)):
	os.system('cd "%s" && start widevine_decrypter "%s" "%s" "%s"'
		%(os.path.abspath('../widevine_decrypter/src/build'),
		  os.path.abspath('../tmp/_data/'+L[i]),
		  os.path.abspath('../tmp/_info'),
		  os.path.abspath('../tmp/_decrypted')))
		  
	time.sleep(1)
