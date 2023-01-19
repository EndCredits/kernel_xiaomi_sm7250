// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"
#include <linux/prefetch.h>

#include <trace/events/erofs.h>

static void erofs_readendio(struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	const blk_status_t err = bio->bi_status;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		/* page is already locked */
		DBG_BUGON(PageUptodate(page));

		if (err)
			SetPageError(page);
		else
			SetPageUptodate(page);

		unlock_page(page);
		/* page could be reclaimed now */
	}
	bio_put(bio);
}

struct page *erofs_get_meta_page(struct super_block *sb, erofs_blk_t blkaddr)
{
	struct address_space *const mapping = sb->s_bdev->bd_inode->i_mapping;
	struct page *page;

	page = read_cache_page_gfp(mapping, blkaddr,
				   mapping_gfp_constraint(mapping, ~__GFP_FS));
	/* should already be PageUptodate */
	if (!IS_ERR(page))
		lock_page(page);
	return page;
}

static int erofs_map_blocks_flatmode(struct inode *inode,
				     struct erofs_map_blocks *map,
				     int flags)
{
	erofs_blk_t nblocks, lastblk;
	u64 offset = map->m_la;
	struct erofs_inode *vi = EROFS_I(inode);
	bool tailendpacking = (vi->datalayout == EROFS_INODE_FLAT_INLINE);

	nblocks = DIV_ROUND_UP(inode->i_size, PAGE_SIZE);
	lastblk = nblocks - tailendpacking;

	/* there is no hole in flatmode */
	map->m_flags = EROFS_MAP_MAPPED;
	if (offset < blknr_to_addr(lastblk)) {
		map->m_pa = blknr_to_addr(vi->raw_blkaddr) + map->m_la;
		map->m_plen = blknr_to_addr(lastblk) - offset;
	} else if (tailendpacking) {
		/* 2 - inode inline B: inode, [xattrs], inline last blk... */
		struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

		map->m_pa = iloc(sbi, vi->nid) + vi->inode_isize +
			vi->xattr_isize + erofs_blkoff(map->m_la);
		map->m_plen = inode->i_size - offset;

		/* inline data should be located in the same meta block */
		if (erofs_blkoff(map->m_pa) + map->m_plen > EROFS_BLKSIZ) {
			erofs_err(inode->i_sb,
				  "inline data cross block boundary @ nid %llu",
				  vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		map->m_flags |= EROFS_MAP_META;
	} else {
		erofs_err(inode->i_sb,
			  "internal error @ nid: %llu (size %llu), m_la 0x%llx",
			  vi->nid, inode->i_size, map->m_la);
		DBG_BUGON(1);
		return -EIO;
	}
	return 0;
}

static int erofs_map_blocks(struct inode *inode,
			    struct erofs_map_blocks *map, int flags)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_chunk_index *idx;
	struct page *page;
	u64 chunknr;
	unsigned int unit;
	erofs_off_t pos;
	int err = 0;
	
	trace_erofs_map_blocks_enter(inode, map, flags);
	if (map->m_la >= inode->i_size) {
		/* leave out-of-bound access unmapped */
		map->m_flags = 0;
		map->m_plen = 0;
		goto out;
	}

	if (vi->datalayout != EROFS_INODE_CHUNK_BASED) {
		err = erofs_map_blocks_flatmode(inode, map, flags);
		goto out;
	}

	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)
		unit = sizeof(*idx);			/* chunk index */
	else
		unit = EROFS_BLOCK_MAP_ENTRY_SIZE;	/* block map */

	chunknr = map->m_la >> vi->chunkbits;
	pos = ALIGN(iloc(EROFS_SB(sb), vi->nid) + vi->inode_isize +
		    vi->xattr_isize, unit) + unit * chunknr;

	page = erofs_get_meta_page(inode->i_sb, erofs_blknr(pos));
	if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto out;
	}
	map->m_la = chunknr << vi->chunkbits;
	map->m_plen = min_t(erofs_off_t, 1UL << vi->chunkbits,
			    roundup(inode->i_size - map->m_la, EROFS_BLKSIZ));

	/* handle block map */
	if (!(vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)) {
		__le32 *blkaddr = page_address(page) + erofs_blkoff(pos);

		if (le32_to_cpu(*blkaddr) == EROFS_NULL_ADDR) {
			map->m_flags = 0;
		} else {
			map->m_pa = blknr_to_addr(le32_to_cpu(*blkaddr));
			map->m_flags = EROFS_MAP_MAPPED;
		}
		goto out_unlock;
	}
	/* parse chunk indexes */
	idx = page_address(page) + erofs_blkoff(pos);
	switch (le32_to_cpu(idx->blkaddr)) {
	case EROFS_NULL_ADDR:
		map->m_flags = 0;
		break;
	default:
		/* only one device is supported for now */
		if (idx->device_id) {
			erofs_err(sb, "invalid device id %u @ %llu for nid %llu",
				  le16_to_cpu(idx->device_id),
				  chunknr, vi->nid);
			err = -EFSCORRUPTED;
			goto out_unlock;
		}
		map->m_pa = blknr_to_addr(le32_to_cpu(idx->blkaddr));
		map->m_flags = EROFS_MAP_MAPPED;
		break;
	}
out_unlock:
	unlock_page(page);
	put_page(page);
out:
	if (!err)
		map->m_llen = map->m_plen;
	trace_erofs_map_blocks_exit(inode, map, flags, 0);
	return err;
}

static inline struct bio *erofs_read_raw_page(struct bio *bio,
					      struct address_space *mapping,
					      struct page *page,
					      erofs_off_t *last_block,
					      unsigned int nblocks,
					      unsigned int *eblks,
					      bool ra)
{
	struct inode *const inode = mapping->host;
	struct super_block *const sb = inode->i_sb;
	erofs_off_t current_block = (erofs_off_t)page->index;
	int err;

	DBG_BUGON(!nblocks);

	if (PageUptodate(page)) {
		err = 0;
		goto has_updated;
	}

	/* note that for readpage case, bio also equals to NULL */
	if (bio &&
	    (*last_block + 1 != current_block || !*eblks)) {
submit_bio_retry:
		submit_bio(bio);
		bio = NULL;
	}

	if (!bio) {
		struct erofs_map_blocks map = {
			.m_la = blknr_to_addr(current_block),
		};
		erofs_blk_t blknr;
		unsigned int blkoff;

		err = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
		if (err)
			goto err_out;

		/* zero out the holed page */
		if (!(map.m_flags & EROFS_MAP_MAPPED)) {
			zero_user_segment(page, 0, PAGE_SIZE);
			SetPageUptodate(page);

			/* imply err = 0, see erofs_map_blocks */
			goto has_updated;
		}

		/* for RAW access mode, m_plen must be equal to m_llen */
		DBG_BUGON(map.m_plen != map.m_llen);

		blknr = erofs_blknr(map.m_pa);
		blkoff = erofs_blkoff(map.m_pa);

		/* deal with inline page */
		if (map.m_flags & EROFS_MAP_META) {
			void *vsrc, *vto;
			struct page *ipage;

			DBG_BUGON(map.m_plen > PAGE_SIZE);

			ipage = erofs_get_meta_page(inode->i_sb, blknr);

			if (IS_ERR(ipage)) {
				err = PTR_ERR(ipage);
				goto err_out;
			}

			vsrc = kmap_atomic(ipage);
			vto = kmap_atomic(page);
			memcpy(vto, vsrc + blkoff, map.m_plen);
			memset(vto + map.m_plen, 0, PAGE_SIZE - map.m_plen);
			kunmap_atomic(vto);
			kunmap_atomic(vsrc);
			flush_dcache_page(page);

			SetPageUptodate(page);
			/* TODO: could we unlock the page earlier? */
			unlock_page(ipage);
			put_page(ipage);

			/* imply err = 0, see erofs_map_blocks */
			goto has_updated;
		}

		/* pa must be block-aligned for raw reading */
		DBG_BUGON(erofs_blkoff(map.m_pa));

		/* max # of continuous pages */
		if (nblocks > DIV_ROUND_UP(map.m_plen, PAGE_SIZE))
			nblocks = DIV_ROUND_UP(map.m_plen, PAGE_SIZE);

		*eblks = bio_max_segs(nblocks);
		bio = bio_alloc(GFP_NOIO, *eblks);

		bio->bi_end_io = erofs_readendio;
		bio_set_dev(bio, sb->s_bdev);
		bio->bi_iter.bi_sector = (sector_t)blknr <<
			LOG_SECTORS_PER_BLOCK;
		bio->bi_opf = REQ_OP_READ | (ra ? REQ_RAHEAD : 0);
	}

	err = bio_add_page(bio, page, PAGE_SIZE, 0);
	/* out of the extent or bio is full */
	if (err < PAGE_SIZE)
		goto submit_bio_retry;
	--*eblks;
	*last_block = current_block;
	return bio;

err_out:
	/* for sync reading, set page error immediately */
	if (!ra) {
		SetPageError(page);
		ClearPageUptodate(page);
	}
has_updated:
	unlock_page(page);

	/* if updated manually, continuous pages has a gap */
	if (bio)
		submit_bio(bio);
	return err ? ERR_PTR(err) : NULL;
}

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int erofs_raw_access_readpage(struct file *file, struct page *page)
{
	erofs_off_t last_block;
	unsigned int eblks;
	struct bio *bio;

	trace_erofs_readpage(page, true);

	bio = erofs_read_raw_page(NULL, page->mapping,
				  page, &last_block, 1, &eblks, false);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (bio)
		submit_bio(bio);
	return 0;
}

static int erofs_raw_access_readpages(struct file *filp,
				      struct address_space *mapping,
				      struct list_head *pages,
				      unsigned int nr_pages)
{
	erofs_off_t last_block;
	unsigned int eblks;
	struct bio *bio = NULL;
	gfp_t gfp = readahead_gfp_mask(mapping);
	struct page *page = list_last_entry(pages, struct page, lru);

	trace_erofs_readpages(mapping->host, page, nr_pages, true);

	for (; nr_pages; --nr_pages) {
		page = list_entry(pages->prev, struct page, lru);

		prefetchw(&page->flags);
		list_del(&page->lru);

		if (!add_to_page_cache_lru(page, mapping, page->index, gfp)) {
			bio = erofs_read_raw_page(bio, mapping, page,
						  &last_block, nr_pages, &eblks, true);

			/* all the page errors are ignored when readahead */
			if (IS_ERR(bio)) {
				pr_err("%s, readahead error at page %lu of nid %llu\n",
				       __func__, page->index,
				       EROFS_I(mapping->host)->nid);

				bio = NULL;
			}
		}

		/* pages could still be locked */
		put_page(page);
	}
	DBG_BUGON(!list_empty(pages));

	if (bio)
		submit_bio(bio);
	return 0;
}

static sector_t erofs_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	struct erofs_map_blocks map = {
		.m_la = blknr_to_addr(block),
	};

	if (EROFS_I(inode)->datalayout == EROFS_INODE_FLAT_INLINE) {
		erofs_blk_t blks = i_size_read(inode) >> LOG_BLOCK_SIZE;

		if (block >> LOG_SECTORS_PER_BLOCK >= blks)
			return 0;
	}

	if (!erofs_map_blocks_flatmode(inode, &map, EROFS_GET_BLOCKS_RAW))
		return erofs_blknr(map.m_pa);

	return 0;
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations erofs_raw_access_aops = {
	.readpage = erofs_raw_access_readpage,
	.readpages = erofs_raw_access_readpages,
	.bmap = erofs_bmap,
};
