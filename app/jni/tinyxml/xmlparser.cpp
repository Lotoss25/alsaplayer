#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>
#include "tinyxml2.h"

extern "C" {
    struct nvset {
	const char *name;
	const char *value;
	const char *append;
	int min, max, flags;
	struct nvset *next;	
    };
    static const struct {
	const char *name;
	int  val;
    } supp_formats[] = { 
	{ "SNDRV_PCM_FORMAT_S16_LE",  (1 << 0) },
	{ "SNDRV_PCM_FORMAT_S24_LE",  (1 << 1) },
	{ "SNDRV_PCM_FORMAT_S24_3LE", (1 << 2) },
	{ "SNDRV_PCM_FORMAT_S32_LE",  (1 << 3) },
    };
#define n_supp_formats int((sizeof(supp_formats))/(sizeof(supp_formats[0])))	
    typedef enum { PERSET_DEFAULT, PERSET_RATE, PERSET_FMT } perset_t;
    struct perset {
	perset_t type;
	int val;
	int periods, period_size;
	struct perset *next;
    };
}

class MixXML : public XMLDocument {
	bool loaded;
	XMLElement *mix;
	XMLElement *find_path(const char *path) {
	    XMLElement *e;
	    for(e = mix->FirstChildElement(); e; e = e->NextSiblingElement())
		if(strcmp(e->Name(),"path") == 0 && e->Attribute("name", path)) break;
	    return e;			
	}
	struct nvset *get_pairs(XMLElement *e);
    public:
	MixXML(const char *file) : mix(0) { 
	    loaded = (LoadFile(file) == 0);  
	    if(loaded) {
		mix = FirstChildElement();
		if(!mix || strcmp(mix->Name(),"mixer") != 0) loaded = false;
	    } 	
	}
	bool is_valid() { return loaded; }
	const char *find_control_default(const char *name) {
	    XMLElement *e; 	
	    for(e = mix->FirstChildElement(); e; e = e->NextSiblingElement())
		if(strcmp(e->Name(),"ctl") == 0 && e->Attribute("name", name) 
		    && e->Attribute("value")) return e->Attribute("value");
	    return 0;
	}
	struct nvset *find_nv_set(const char *path) {
	    XMLElement *e = find_path(path);
		if(!e) return 0;
		return get_pairs(e);	
	}
}; 



struct nvset *MixXML::get_pairs(XMLElement *pe)
{
    XMLElement *e;
    struct nvset *first = 0, *nv = 0;
    for(e = pe->FirstChildElement(); e; e = e->NextSiblingElement()) {
	if(strcmp(e->Name(), "ctl") == 0) {
	    if(!e->Attribute("name") || !e->Attribute("value")) continue;
	    if(!first) nv = first = (struct nvset *) malloc(sizeof(struct nvset));
	    else {	
		nv->next = (struct nvset *) malloc(sizeof(struct nvset)); 
		nv = nv->next;
	    }
	    nv->name = e->Attribute("name");
	    nv->value = e->Attribute("value");
	    nv->next = 0;
	} else if(strcmp(e->Name(), "path") == 0) {
	    const char *c = e->Attribute("name"); 
	    if(!c) continue;
	    XMLElement *e1 = find_path(c);
	    if(!e1) continue;
	    struct nvset *nv1 = get_pairs(e1);	/** recursion with no checks for recursive paths! **/
	    if(!nv1)  continue;
	    if(!first) nv = first = nv1;
	    else nv->next = nv1;
	    while(nv->next) nv = nv->next;
	}
    }
    return first;
}

extern "C" void *xml_mixp_open(const char *file)
{
    MixXML *m = new MixXML(file);
    if(!m->is_valid()) {
	delete m;
	return 0;
    }
    return (void *) m;	
}

extern "C" void xml_mixp_close(void *xml)
{ 
    MixXML *m = (MixXML *) xml;
    if(m) delete m;
}

extern "C" const char *xml_mixp_find_control_default(void *xml, const char *name) 
{
    MixXML *m = (MixXML *) xml;
    if(!m || !m->is_valid()) return 0;
    return m->find_control_default(name);	
}

extern "C" struct nvset *xml_mixp_find_control_set(void *xml, const char *path) 
{
    MixXML *m = (MixXML *) xml;
    if(!m || !m->is_valid()) return 0;
    return m->find_nv_set(path);
}

////////////////////////////////////////////////////////////////////////

class DeviceXML : public XMLDocument {
	bool card_only;		/* used only to detect if specific devices for the card are found in xml */
	XMLElement *card_root;
	XMLElement *dev_root;
    public:
	DeviceXML(const char *file, const char *card, const char *device); /* device = 0 means card_only */
	bool is_valid() { return (dev_root != 0); }
	bool is_valid(const char *dev_str);
	bool is_card_only() { return card_only; }
	bool is_builtin() { return card_root && (card_root->Attribute("builtin", "1") != 0); }
	bool is_offload() { return card_root && dev_root && (dev_root->Attribute("offload", "1") != 0); }
	bool is_mmapped() { return card_root && dev_root && (dev_root->Attribute("mmap", "1") != 0); }
	XMLElement *get_card_root() { return card_root; };
	XMLElement *get_dev_root() { return dev_root; };
	struct nvset *get_controls(XMLElement *e);
}; 

DeviceXML::DeviceXML(const char *file, const char *card, const char *device) 
{
    XMLElement *elem;	

    card_root = 0; 
    dev_root = 0;
    card_only = false;

    if(LoadFile(file) != 0) return;

    for(elem = FirstChildElement(); elem; elem = elem->NextSiblingElement()) 
	if(strcmp(elem->Name(), "cards") == 0) break;
    if(!elem) return;

    for(card_root = elem->FirstChildElement(); card_root; card_root = card_root->NextSiblingElement()) 
	if(strcmp(card_root->Name(), "card") == 0 && card_root->Attribute("name")) {
	    int result;
	    regex_t re;
	    const char *c = card_root->Attribute("name");
	    	if(regcomp(&re, c, REG_NOSUB | REG_EXTENDED) != 0) {
		    card_root = 0;
		    break;		
		}
		result = regexec(&re, card, 0, 0, 0);
		regfree(&re);
		if(result == 0) break;
	}
    if(!card_root) return;
    if(!device)	{
	card_only = true;
	dev_root = card_root->FirstChildElement();
	return;
    }
    for(dev_root = card_root->FirstChildElement(); dev_root; dev_root = dev_root->NextSiblingElement()) 
	if(strcmp(dev_root->Name(), "device") == 0 && dev_root->Attribute("id", device)) break;
	

}

bool DeviceXML::is_valid(const char *dev_str) 
{
    XMLElement *dev = 0;
    for(dev = dev_root; dev; dev = dev->NextSiblingElement())
	if(strcmp(dev->Name(), "device") == 0 && dev->Attribute("id", dev_str)) break;
    return (dev != 0);
}

struct nvset *DeviceXML::get_controls(XMLElement *e) 
{
    XMLElement *x, *r;
    struct nvset *first = 0, *nv;
    const char *name, *value, *min, *max, *flags;	
    for(x = e->FirstChildElement(); x; x = x->NextSiblingElement()) {
	if(strcmp(x->Name(), "ctl") == 0) {
	    name = x->Attribute("name");
	    value = x->Attribute("value");
	    if(!name || !value) continue;
	    if(!first) nv = first = (struct nvset *) malloc(sizeof(struct nvset));
	    else {
		nv->next = (struct nvset *) malloc(sizeof(struct nvset));	
		nv = nv->next;
	    }
	    min = x->Attribute("min");
	    max = x->Attribute("max");
	    if(min && max) {
		nv->min = atoi(min);
		nv->max = atoi(max);
	    } else {
		nv->min = 0;
		nv->max = 0;
	    }	
	    flags = x->Attribute("flags");
	    nv->flags = flags ? (int) strtoul(flags,0,0) : 0;	
	    nv->name = name;
	    nv->value = value;
	    nv->append = x->Attribute("append");
	    nv->next = 0;
	} else if(strcmp(x->Name(), "path") == 0) {
	    const char *c = x->Attribute("name");
	    if(!c) continue;		
	    for(r = get_card_root()->FirstChildElement(); r; r = r->NextSiblingElement()) {
		if(strcmp(r->Name(), "path") == 0 && r->Attribute("name", c)) {
		    first = get_controls(r);
		    for(nv = first; nv; nv = nv->next) if(!nv->next) break;
		}
	    }
	}
    }
    return first;
}

extern "C" void *xml_dev_open(const char *xml_path, const char *card, int device)
{
    char dev_str[16];
    const char *dev;	

    if(device >= 0) {
	sprintf(dev_str, "%d", device);
	dev = dev_str;
    } else dev = 0;	/* matches any device */

    DeviceXML *xml = new DeviceXML(xml_path, card, dev);
    if(!xml->is_valid()) {
	delete xml;
	return 0;
    }
    return (void *) xml;	
}

extern "C" void xml_dev_close(void *xml)
{
    if(!xml) return;
    delete (DeviceXML *) xml;	
} 

extern "C" int xml_dev_is_builtin(void *xml)
{
    if(!xml) return 0;
    return (int) ((DeviceXML *) xml)->is_builtin();
}

extern "C" int xml_dev_is_offload(void *xml)
{
    if(!xml) return 0;
    return (int) ((DeviceXML *) xml)->is_offload();
}

extern "C" int xml_dev_is_mmapped(void *xml)
{
    if(!xml) return 0;
    return (int) ((DeviceXML *) xml)->is_mmapped();
}

extern "C" int xml_dev_exists(void *xml, int device) 
{
    char dev_str[16];
	if(!xml) return 0;
	sprintf(dev_str, "%d", device);
    return (int) ((DeviceXML *) xml)->is_valid(dev_str);
}


extern "C" struct nvset *xml_dev_find_ctls(void *xml, const char *name, const char *value)
{
    DeviceXML *m = (DeviceXML *) xml; 	
    if(!m || !m->is_valid() || m->is_card_only()) return 0;	
    XMLElement *e, *e1, *e2;


    for(e = m->get_dev_root()->FirstChildElement(); e; e = e->NextSiblingElement()) {
	if(strcmp(e->Name(), "path") == 0 && e->Attribute("name", name)
	   && (!value || e->Attribute("value", value))) return m->get_controls(e);
	const char *c = e->Attribute("name");
	if(!c) continue;
	for(e1 = m->get_card_root()->FirstChildElement(); e1; e1 = e1->NextSiblingElement()) {
	    if(strcmp(e1->Name(), "path") == 0 && e1->Attribute("name", c)) {
		for(e2 = e1->FirstChildElement(); e2; e2 = e2->NextSiblingElement()) {
		    if(strcmp(e2->Name(), "path") == 0 && e2->Attribute("name", name)
		       && (!value || e2->Attribute("value", value) )) return m->get_controls(e2);
		}
	    }	
	}    	
    }
    return 0;	
}

/*
 <path name="preset" [one of: rate="NNN", fmt="SNDRV_PCM_FORMAT_XXX", or default="1"] value="32:4096"/> 
*/

static struct perset *add_perset(struct perset *pers, XMLElement *e)
{
    int k;
    struct perset *pp, p = { PERSET_DEFAULT, 0, 0, 0, 0 };
    const char *c;

	c = e->Attribute("value");
	if(!c || sscanf(c, "%d:%d", &p.periods, &p.period_size) != 2) return 0;

	c = e->Attribute("rate");
	if(c) { 
	    p.val = atoi(c);
	    if(p.val <= 0) return 0;
	    p.type = PERSET_RATE; 
	} else {
	    c = e->Attribute("fmt");
	    if(c) {	
		for(k = 0; k < n_supp_formats; k++) if(strcmp(c, supp_formats[k].name) == 0) break;
		if(k == n_supp_formats) return 0;
		p.type = PERSET_FMT;
		p.val = supp_formats[k].val;
	    } else {
		if(!e->Attribute("default", "1")) return 0;	
		p.val = 1;
	    }
	}
        pp = (struct perset *) malloc(sizeof(struct perset));
	if(!pp) return 0;
	*pp = p;
	pp->next = pers;	

    return pp;
}

extern "C" struct perset *xml_dev_find_persets(void *xml)
{
    DeviceXML *m = (DeviceXML *) xml; 	
    if(!m || !m->is_valid() || m->is_card_only()) return 0;	
    XMLElement *e, *e1, *e2;
    struct perset *ret = 0;

    for(e = m->get_dev_root()->FirstChildElement(); e; e = e->NextSiblingElement()) {
	if(strcmp(e->Name(), "path") == 0 && e->Attribute("name", "perset")) {
	    ret = add_perset(ret, e); 
	    continue;	
	}
	const char *c = e->Attribute("name");
	if(!c) continue;
	for(e1 = m->get_card_root()->FirstChildElement(); e1; e1 = e1->NextSiblingElement()) {
	    if(strcmp(e1->Name(), "path") == 0 && e1->Attribute("name", c)) {
		for(e2 = e1->FirstChildElement(); e2; e2 = e2->NextSiblingElement()) {
		    if(strcmp(e2->Name(), "path") == 0 && e2->Attribute("name", "perset")) {
	    		ret = add_perset(ret, e); 
		    }
		}
	    }
	}
    }
    return ret;	
}

extern "C" int xml_get_mixer_path(void *xml, char* path, size_t length)
{
	DeviceXML *m = (DeviceXML *) xml;
	const char* val = 0;
	if(!m || !m->is_valid()) return 0;
	val = m->get_card_root()->Attribute("mixer_path");
	if (val) strncpy(path, val, length);
	return 1;
}

#ifdef ACDB_TEST
extern "C" int xml_get_acdb_id(const char *file, const char *devname) 
{
    XMLDocument *doc = new XMLDocument;
    XMLElement *root, *sect, *elm;
    int ret = -1;

	if(doc->LoadFile(file) != 0) goto done;
	root = doc->FirstChildElement();
	if(!root || strcmp(root->Name(), "audio_platform_info") != 0) goto done;
	for(sect = root->FirstChildElement(); sect; sect = sect->NextSiblingElement()) {
	    if(strcmp(sect->Name(), "acdb_ids") != 0) continue;
	    for(elm = sect->FirstChildElement(); elm; elm = elm->NextSiblingElement()) {	
		if(strcmp(elm->Name(), "device") == 0 
			&& elm->Attribute("name", devname) && elm->Attribute("acdb_id")) {
		    ret = atoi(elm->Attribute("acdb_id"));
		    break;	
		}
	    }
	}
    done:
	delete doc;
	return ret;
}
#endif

