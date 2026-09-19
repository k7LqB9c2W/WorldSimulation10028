"""Prepare the offline WGS84 game world. See Research/GIS_WORLD.md.

Dependencies: requests numpy pillow rasterio geopandas scipy.
Raw downloads live in ignored out/gis_sources; never download at game startup.
"""
from pathlib import Path
import concurrent.futures
import requests
import zipfile
import json
import hashlib
import argparse
import re
import shutil
import time
import numpy as np
from PIL import Image
import rasterio
from rasterio.enums import Resampling
from rasterio.warp import reproject
from rasterio.transform import from_bounds
from rasterio.features import rasterize
import geopandas as gpd
import pandas as pd
from scipy.ndimage import distance_transform_edt, binary_dilation
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'out/gis_sources'
CACHE.mkdir(parents=True, exist_ok=True)
sys.path.insert(0,str(CACHE/'python_deps'))
SOURCES = {
    'natural_earth.zip': 'https://naciscdn.org/naturalearth/10m/raster/NE2_LR_LC_SR_W.zip',
    'land.zip': 'https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip',
    'countries.zip': 'https://naciscdn.org/naturalearth/10m/cultural/ne_10m_admin_0_countries.zip',
    'etopo.tif': 'https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif',
    'rivers.zip': 'https://data.hydrosheds.org/file/HydroRIVERS/HydroRIVERS_v10_shp.zip',
    'mrds.zip': 'https://mrdata.usgs.gov/mrds/mrds-csv.zip',
    'lakes.zip': 'https://naciscdn.org/naturalearth/10m/physical/ne_10m_lakes.zip',
    'wocqi.xls': 'https://pubs.usgs.gov/of/2010/1196/data/WoCQI_v1_1.xls',
}
WIDTH, HEIGHT = 1920, 960
TRANSFORM = from_bounds(-180, -90, 180, 90, WIDTH, HEIGHT)
STAGE = CACHE / 'prepared'

def sample_raster(path, width=WIDTH, height=HEIGHT, resampling=Resampling.average):
    """Read an overview first so global rasters never expand to full native size."""
    with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN='EMPTY_DIR', GDAL_HTTP_TIMEOUT='60'):
        with rasterio.open(path) as src:
            sh = min(src.height, height * 2)
            sw = min(src.width, width * 2)
            data = src.read(1, out_shape=(sh, sw), masked=True, resampling=resampling).astype('float32')
            data = data.filled(np.nan) * src.scales[0] + src.offsets[0]
            dst = np.full((height, width), np.nan, dtype='float32')
            reproject(data, dst,
                      src_transform=src.transform * src.transform.scale(src.width/sw, src.height/sh),
                      src_crs=src.crs, src_nodata=np.nan,
                      dst_transform=from_bounds(-180,-90,180,90,width,height),
                      dst_crs='EPSG:4326', dst_nodata=np.nan, resampling=resampling)
            return dst

def fetch_environment():
    for prop in ['clay', 'phh2o']:
        params = {'map': f'/map/{prop}.map', 'SERVICE': 'WCS', 'VERSION':'1.0.0',
                  'REQUEST':'GetCoverage', 'COVERAGE':f'{prop}_0-5cm_mean',
                  'CRS':'EPSG:4326', 'RESPONSE_CRS':'EPSG:4326',
                  'BBOX':'-180,-60,180,85','WIDTH':1920,'HEIGHT':774,'FORMAT':'GEOTIFF_INT16'}
        url = requests.Request('GET','https://maps.isric.org/mapserv',params=params).prepare().url
        SOURCES[f'soil_{prop}.tif'] = url
        download((f'soil_{prop}.tif',url))
    # Coarse temporal sampling of reconstructed annual climate. Century labels
    # are nominal game dates, not claims of precise annual reconstruction.
    jobs=[]
    for century in [-200,-150,-100,-50,0,20]:
        for var in ['bio01','bio12']:
            code=f'{century:04d}'
            url=f'https://os.zhdk.cloud.switch.ch/chelsa01/chelsa_trace21k/global/bioclim/{var}/CHELSA_TraCE21k_{var}_{code}_V.1.0.tif'
            jobs.append((century,var,url))
    def fetch(job):
        century,var,url=job
        dest=CACHE/f'{var}_{century}.npy'
        if not dest.exists():
            a=sample_raster(url,320,160)
            if var=='bio01': a-=273.15
            np.save(dest,a)
        a=np.load(dest)
        print('Climate:',century,var,'range',np.nanmin(a),np.nanmax(a),flush=True)
        return {'century':century,'variable':var,'url':url}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        entries=list(pool.map(fetch,jobs))
    (CACHE/'environment_sources.json').write_text(json.dumps({'climate':entries,'soils':{k:v for k,v in SOURCES.items() if k.startswith('soil_')}},indent=2))

def download(item):
    name, url = item
    dest = CACHE / name
    if dest.exists():
        print('Cached:', name, dest.stat().st_size, flush=True)
        return dest
    print('Downloading:', name, url, flush=True)
    temp = dest.with_suffix(dest.suffix + '.part')
    with requests.get(url, stream=True, timeout=(30, 120)) as response:
        response.raise_for_status()
        with temp.open('wb') as output:
            for chunk in response.iter_content(1024*1024):
                output.write(chunk)
        expected = response.headers.get('Content-Length')
        if expected and temp.stat().st_size != int(expected):
            raise RuntimeError(f'Truncated download: {name}')
    for attempt in range(10):
        try:
            temp.replace(dest)
            break
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(1)
    print('Downloaded:', name, dest.stat().st_size, flush=True)
    return dest

def save_png(name, array):
    Image.fromarray(array.astype('uint8')).save(STAGE/name)

def colored_mask(mask, color):
    a=np.zeros((HEIGHT,WIDTH,4),dtype='uint8')
    a[mask,:3]=color
    a[mask,3]=255
    return a

def prepare():
    STAGE.mkdir(parents=True,exist_ok=True)
    print('Preparing land and visual map...',flush=True)
    land_df=gpd.read_file(CACHE/'land.zip').to_crs(4326)
    land=rasterize(((g,1) for g in land_df.geometry),out_shape=(HEIGHT,WIDTH),transform=TRANSFORM,dtype='uint8').astype(bool)
    lakes=gpd.read_file(CACHE/'lakes.zip').to_crs(4326)
    lakes=lakes[lakes.featurecla!='Reservoir']
    lake=rasterize(((g,1) for g in lakes.geometry),out_shape=(HEIGHT,WIDTH),transform=TRANSFORM,dtype='uint8').astype(bool)
    land &= ~lake
    save_png('landmask.png',land.astype('uint8')*255)
    with zipfile.ZipFile(CACHE/'natural_earth.zip') as z:
        z.extractall(CACHE/'natural_earth')
    with rasterio.open(CACHE/'natural_earth/NE2_LR_LC_SR_W.tif') as src:
        rgb=src.read(out_shape=(3,HEIGHT,WIDTH),resampling=Resampling.average).transpose(1,2,0)
    # Single authoritative land boundary. Fill shoreline disagreement from the
    # nearest interior pixel; water gets a consistent readable ocean color.
    rgb[~land]=[43,79,105]
    save_png('map.png',rgb)
    elevation=sample_raster(CACHE/'etopo.tif')
    if not np.isfinite(elevation).all(): raise ValueError('ETOPO has missing cells')
    # Preserve current engine elevation contract: normalized 0..1, 9000 m ceiling.
    save_png('heightmap.png',np.rint(np.clip(elevation,0,9000)/9000*255))
    with rasterio.open(STAGE/'elevation_m.tif','w',driver='GTiff',height=HEIGHT,width=WIDTH,count=1,
                       dtype='float32',crs='EPSG:4326',transform=TRANSFORM,compress='deflate') as dst:
        dst.write(elevation,1)
    print('Preparing connected river corridors...',flush=True)
    rivers=gpd.read_file('zip://'+(CACHE/'rivers.zip').as_posix()+'!HydroRIVERS_v10_shp/HydroRIVERS_v10.shp',where='DIS_AV_CMS >= 10',columns=['DIS_AV_CMS','ORD_STRA']).to_crs(4326)
    river=rasterize(((g,1) for g in rivers.geometry if g is not None),out_shape=(HEIGHT,WIDTH),transform=TRANSFORM,all_touched=True,dtype='uint8').astype(bool)
    river &= land
    save_png('riverland.png',colored_mask(river,(24,255,239)))
    # Show major reaches at world zoom; the full network remains in riverland.
    major=rasterize(((g,1) for g in rivers[rivers.DIS_AV_CMS>=1000].geometry if g is not None),out_shape=(HEIGHT,WIDTH),transform=TRANSFORM,all_touched=True,dtype='uint8').astype(bool)&land
    rgb[major]=(0.45*rgb[major]+0.55*np.array([65,117,142])).astype('uint8')
    save_png('map.png',rgb)
    print('Preparing mineral occurrences...',flush=True)
    with zipfile.ZipFile(CACHE/'mrds.zip') as z:
        with z.open('mrds.csv') as f: records=pd.read_csv(f,low_memory=False,encoding='utf-8',encoding_errors='replace')
    coal_samples=pd.read_excel(CACHE/'wocqi.xls',header=1)
    coal_records=pd.DataFrame({'longitude':coal_samples['LONGITUDE IN DECIMAL DEGREES'],
                               'latitude':coal_samples['LATITUDE IN DECIMAL DEGREES'],
                               'commod1':'Coal','commod2':'','commod3':''})
    records=pd.concat([records,coal_records],ignore_index=True)
    lon=pd.to_numeric(records.longitude,errors='coerce').to_numpy()
    lat=pd.to_numeric(records.latitude,errors='coerce').to_numpy()
    valid=np.isfinite(lon)&np.isfinite(lat)&(lon>=-180)&(lon<=180)&(lat>=-90)&(lat<=90)
    commodity=records[['commod1','commod2','commod3']].fillna('').agg(';'.join,axis=1).str.lower()
    # Point records mark their containing cell. Nearby land snapping is allowed
    # only within 2 pixels (~42 km at the equator) to reconcile coastal geometry.
    distance,nearest=distance_transform_edt(~land,return_indices=True)
    counts={}
    colors={'gold':(242,227,21),'iron':(0,0,0),'salt':(178,0,255),
            'copper':(136,78,68),'tin':(39,135,132),'coal':(53,0,62)}
    patterns={'gold':r'\bgold\b','iron':r'\biron\b','salt':r'\bsalt\b|\bhalite\b',
              'copper':r'\bcopper\b','tin':r'\btin\b','coal':r'\bcoal\b|\blignite\b'}
    for name,color in colors.items():
        sel=valid & commodity.str.contains(patterns[name],regex=True).to_numpy()
        xs=np.clip(((lon[sel]+180)*WIDTH/360).astype(int),0,WIDTH-1)
        ys=np.clip(((90-lat[sel])*HEIGHT/180).astype(int),0,HEIGHT-1)
        near=distance[ys,xs]<=2
        xs,ys=xs[near],ys[near]
        yy,xx=nearest[:,ys,xs]
        mask=np.zeros((HEIGHT,WIDTH),bool); mask[yy,xx]=True
        save_png(name+'.png',colored_mask(mask,color))
        counts[name]={'source_records':int(sel.sum()),'land_cells':int(mask.sum()),'unplaced_records':int((~near).sum())}
    # Legacy combined file holds only modeled horse habitat. Mineral layers are
    # independent, so an iron record cannot erase gold or salt in the same cell.
    yy,xx=np.indices((HEIGHT,WIDTH)); latitude=90-(yy+.5)*180/HEIGHT; longitude=-180+(xx+.5)*360/WIDTH
    horses=land&(longitude>25)&(longitude<115)&(latitude>40)&(latitude<55)&(elevation<2000)
    save_png('resource.png',colored_mask(horses,(127,0,55)))
    print('Preparing scenario spawn regions...',flush=True)
    countries=gpd.read_file(CACHE/'countries.zip').to_crs(4326)
    text=(ROOT/'src/simulation_context.cpp').read_text(encoding='utf-8')
    block=text.split('SimulationConfig::defaultSpawnRegions() {')[1].split('\n}')[0]
    entries=re.findall(r'\{"([^"]+)", "[^"]+", (\d+), (\d+), (\d+),',block)
    palette={key:tuple(map(int,(r,g,b))) for key,r,g,b in entries}
    region=np.full((HEIGHT,WIDTH),-1,dtype='int16')
    keys=list(palette)
    for _,row in countries.iterrows():
        cont=row.CONTINENT; sub=row.SUBREGION; name=row.ADMIN
        key={'Africa':'cs_africa','Asia':'cn_asia','Europe':'central_europe','Oceania':'oceania',
             'North America':'e_na','South America':'andes'}.get(cont)
        key={'Southern Asia':'south_asia','Eastern Asia':'east_asia','South-Eastern Asia':'se_asia',
             'Western Asia':'west_asia','Northern Africa':'north_africa','Western Africa':'west_africa',
             'Eastern Africa':'east_africa','Caribbean':'caribbean','Central America':'mesoamerica',
             'Northern Europe':'north_europe','Western Europe':'wnw_europe','Southern Europe':'med_europe'}.get(sub,key)
        if name in ['Egypt','Sudan','Ethiopia','Eritrea','Djibouti']: key='nile_ne_africa'
        if name in ['Greece','Albania','Bulgaria','Romania','Serbia','Kosovo','Montenegro','North Macedonia','Bosnia and Herzegovina']: key='se_europe'
        if name=='Mexico': key='mesoamerica'
        if key is None: continue
        m=rasterize([(row.geometry,1)],out_shape=(HEIGHT,WIDTH),transform=TRANSFORM,dtype='uint8').astype(bool)
        region[m]=keys.index(key)
    region[(region==keys.index('e_na'))&(longitude< -100)]=keys.index('w_na')
    region[(region==keys.index('central_europe'))&(longitude>45)]=keys.index('cn_asia')
    spawn=np.zeros((HEIGHT,WIDTH,4),dtype='uint8')
    spawn_counts={}
    for key,color in palette.items():
        m=land&(region==keys.index(key))
        spawn[m,:3]=color; spawn[m,3]=255
        spawn_counts[key]=int(m.sum())
        if not m.any(): raise ValueError('Empty spawn region: '+key)
    save_png('spawn.png',spawn)
    print('Preparing soil and climate fields...',flush=True)
    clay=sample_raster(CACHE/'soil_clay.tif') / 1000 # g/kg to fraction
    ph=sample_raster(CACHE/'soil_phh2o.tif') / 10
    known=land&np.isfinite(clay)&np.isfinite(ph)
    soil=np.zeros((HEIGHT,WIDTH,4),dtype='uint8')
    soil[...,0]=np.rint(np.nan_to_num(np.clip(clay,0,1))*255).astype('uint8')
    soil[...,1]=np.rint(np.nan_to_num(np.clip(ph,0,14))/14*255).astype('uint8')
    # Explicit gameplay proxy, not a measured fertility/yield product.
    suitability=np.clip((1-.65*np.abs(ph-6.5)/4)*(1-.5*np.abs(clay-.25)),.25,1)
    soil[...,2]=np.rint(np.nan_to_num(suitability,nan=.7)*255).astype('uint8')
    soil[...,3]=known.astype('uint8')*255
    save_png('soil.png',soil)
    # Climate uses the engine's 6x6 field grid, with transparent missing data
    # represented by sentinel values and procedural fallback at runtime.
    fw,fh=WIDTH//6,HEIGHT//6
    centuries=[-200,-150,-100,-50,0,20]
    with (STAGE/'climate.bin').open('wb') as f:
        f.write(b'WSCLIM01'); f.write(struct.pack('<III',fw,fh,len(centuries)))
        for century in centuries:
            f.write(struct.pack('<i',century*100))
            for var in ['bio01','bio12']:
                a=np.load(CACHE/f'{var}_{century}.npy')
                if a.shape!=(fh,fw): raise ValueError('Climate shape mismatch')
                a=np.nan_to_num(a,nan=-9999).astype('<f4')
                f.write(a.tobytes())
    manifest={'version':1,'crs':'EPSG:4326','width':WIDTH,'height':HEIGHT,
              'bounds':[-180,-90,180,90],'pixel_size_degrees':360/WIDTH,
              'sources':SOURCES,'environment':json.loads((CACHE/'environment_sources.json').read_text()),
              'land_cells':int(land.sum()),'river_cells':int(river.sum()),'river_min_discharge_m3_s':10,
              'minerals':counts,'spawn_cells':spawn_counts,'soil_known_cells':int(known.sum()),
              'notes':['Modern coastline and elevation; not reconstructed ancient shorelines.',
                       'Mineral points are incomplete survey observations, not reserves or ancient mine accessibility.',
                       'Horse habitat and spawn regions are scenario approximations, not archaeological datasets.',
                       'Climate is interpolated between six nominal century-labelled snapshots; no annual precision is implied.',
                       'Soil WCS extracts are coarse samples of modern predictions, not ancient soil reconstructions.']}
    for path in STAGE.glob('*.png'):
        with Image.open(path) as im:
            if im.size!=(WIDTH,HEIGHT): raise ValueError('Wrong dimensions: '+str(path))
    manifest['sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in STAGE.iterdir() if p.is_file() and p.name!='manifest.json'}
    (STAGE/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps({'land':manifest['land_cells'],'rivers':manifest['river_cells'],'minerals':counts,'soil':manifest['soil_known_cells']},indent=2),flush=True)

def install():
    if not (STAGE/'manifest.json').exists(): raise RuntimeError('Prepare first')
    dest=ROOT/'assets/images'
    backup=CACHE/'legacy_images'
    backup.mkdir(exist_ok=True)
    for path in STAGE.iterdir():
        target=dest/path.name
        if target.exists() and not (backup/path.name).exists(): shutil.copy2(target,backup/path.name)
        shutil.copy2(path,target)
    print('Installed prepared world; original assets backed up in',backup,flush=True)

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--environment', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--install', action='store_true')
    args=parser.parse_args()
    if args.install:
        install()
    elif args.prepare:
        prepare()
    elif args.environment:
        fetch_environment()
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
            list(pool.map(download, SOURCES.items()))
