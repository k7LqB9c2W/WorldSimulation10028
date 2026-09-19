"""Geographic and packaging checks for the installed GIS world (no network)."""
from pathlib import Path
import hashlib
import json
import struct
import numpy as np
from PIL import Image
import rasterio

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets/images'
manifest=json.loads((ASSETS/'manifest.json').read_text())
for name,digest in manifest['sha256'].items():
    assert hashlib.sha256((ASSETS/name).read_bytes()).hexdigest()==digest, name+' checksum'
land=np.array(Image.open(ASSETS/'landmask.png'))
assert land.shape==(960,1920)
assert set(np.unique(land))=={0,255}
def cell(lon,lat): return int((90-lat)*960/180),int((lon+180)*1920/360)
for lon,lat in [(20,25),(85,32),(-100,40),(135,-25)]:
    assert land[cell(lon,lat)]==255, ('expected land',lon,lat)
for lon,lat in [(0,0),(-140,0),(65,-30),(51,42),(-87,44)]:
    assert land[cell(lon,lat)]==0, ('expected ocean/lake',lon,lat)
colors={'coal':(53,0,62),'copper':(136,78,68),'tin':(39,135,132),'iron':(0,0,0),
        'gold':(242,227,21),'salt':(178,0,255),'riverland':(24,255,239)}
for name,color in colors.items():
    a=np.array(Image.open(ASSETS/(name+'.png')).convert('RGBA'))
    assert a.shape==(960,1920,4)
    valid=a[...,3]>0
    assert valid.any(),name+' empty'
    assert (land[valid]==255).all(),name+' on water'
    assert (a[valid,:3]==color).all(),name+' colors'
spawn=np.array(Image.open(ASSETS/'spawn.png').convert('RGBA'))
assert not np.any((spawn[...,3]>0)&(land==0))
assert all(n>0 for n in manifest['spawn_cells'].values())
with rasterio.open(ASSETS/'elevation_m.tif') as src:
    height=src.read(1)
    assert src.crs.to_epsg()==4326
    assert height[cell(85,32)]>3500, 'Tibetan Plateau elevation'
    assert height[cell(-140,0)]<0, 'Pacific bathymetry'
with (ASSETS/'climate.bin').open('rb') as f:
    assert f.read(8)==b'WSCLIM01'
    w,h,n=struct.unpack('<III',f.read(12)); assert (w,h,n)==(320,160,6)
    years=[]
    for _ in range(n):
        year=struct.unpack('<i',f.read(4))[0];years.append(year)
        t=np.frombuffer(f.read(w*h*4),dtype='<f4').reshape(h,w)
        p=np.frombuffer(f.read(w*h*4),dtype='<f4').reshape(h,w)
        assert np.isfinite(t).all() and np.isfinite(p).all()
        assert np.all((t==-9999)|((t>=-100)&(t<=70)))
        assert np.all((p==-9999)|((p>=0)&(p<=50000)))
        if year==-5000:
            def at(a,lon,lat):return a[int((90-lat)*h/180),int((lon+180)*w/360)]
            assert at(t,20,0)>at(t,20,70), 'Climate latitude/orientation'
            assert at(p,-65,-5)>at(p,20,25), 'Amazon/Sahara precipitation'
    assert years==sorted(set(years)) and not f.read(1)
print('PASS: file hashes, dimensions, geographic landmarks, natural lakes, terrain, resource colors/land alignment, all spawn regions, and climate values.')
