import numpy as np, struct, sys
from PIL import Image
b=open(sys.argv[1],'rb').read(); w,h=struct.unpack('<ii',b[4:12])
ref=np.frombuffer(b[12:12+w*h*4],np.uint8).reshape(h,w,4)[:,:,:3].astype(float)@[0.2126,0.7152,0.0722]
names=['LG13','LGB13','LG11','Helv16','HelvB16','Times16','TimesIt16','Hira16']
print('%-8s'%'rule'+''.join('%9s'%n for n in names[:7])+'   mean(7)')
for spec in sys.argv[2:]:
    name,path=spec.split('=',1)
    img=np.asarray(Image.open(path),float)[:264,:720]
    out=[]
    for i in range(7):
        base=17+30*i; rows=slice(base-16,base+6)
        a=img[rows]; r=ref[rows]; ink=(a<253)|(r<253); out.append(np.abs(a-r)[ink].mean())
    print('%-8s'%name+''.join('%9.1f'%v for v in out)+'   %6.2f'%np.mean(out))
