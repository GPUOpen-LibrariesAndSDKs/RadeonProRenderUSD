#include "resourceRegistry.h"

#include "pxr/imaging/hd/bufferArray.h"

#include <boost/make_shared.hpp>


/*
PXR_NAMESPACE_OPEN_SCOPE

class HdRprStrategy final : public HdAggregationStrategy
{
public:
	class _BufferArray : public HdBufferArray
	{
	public:
		_BufferArray(TfToken const &role,
			HdBufferSpecVector const &bufferSpecs)
			: HdBufferArray(role, TfToken("perfToken")),
			_bufferSpecs(bufferSpecs)
		{}

		virtual ~_BufferArray() {}

		virtual bool GarbageCollect() { return true; }

		virtual void DebugDump(std::ostream &out) const {}

		bool Resize(int numElements) { return true; }

		virtual void
			Reallocate(std::vector<HdBufferArrayRangeSharedPtr> const &,
				HdBufferArraySharedPtr const &) {
		}

		virtual size_t GetMaxNumElements() const {
			TF_VERIFY(false, "unimplemented");
			return 0;
		}

		HdBufferSpecVector GetBufferSpecs() const {
			return _bufferSpecs;
		}

		HdBufferSpecVector _bufferSpecs;
	};

	class _BufferArrayRange : public HdBufferArrayRange
	{
	public:
		_BufferArrayRange() : _bufferArray(nullptr), _numElements(0) {
		}

		virtual bool IsValid() const {
			return _bufferArray;
		}

		virtual bool IsAssigned() const {
			return _bufferArray;
		}

		virtual bool IsImmutable() const {
			return _bufferArray && _bufferArray->IsImmutable();
		}

		virtual bool Resize(int numElements) {
			_numElements = numElements;
			return _bufferArray->Resize(numElements);
		}

		virtual void CopyData(HdBufferSourceSharedPtr const &bufferSource) {
		
		}

		virtual VtValue ReadData(TfToken const &name) const {
			return VtValue();
		}

		virtual int GetOffset() const {
			return 0;
		}

		virtual int GetIndex() const {
			return 0;
		}

		virtual int GetNumElements() const {
			return _numElements;
		}

		virtual int GetCapacity() const {
			TF_VERIFY(false, "unimplemented");
			return 0;
		}

		virtual size_t GetVersion() const {
			return 0;
		}

		virtual void IncrementVersion() {
		}

		virtual size_t GetMaxNumElements() const {
			return _bufferArray->GetMaxNumElements();
		}

		virtual void SetBufferArray(HdBufferArray *bufferArray) {
			_bufferArray = static_cast<_BufferArray *>(bufferArray);
		}

		/// Debug dump
		virtual void DebugDump(std::ostream &out) const {
			out << "Hd_NullStrategy::_BufferArray\n";
		}

		virtual void AddBufferSpecs(HdBufferSpecVector *bufferSpecs) const {
		}

		virtual const void *_GetAggregation() const {
			return _bufferArray;
		}

	private:
		_BufferArray * _bufferArray;
		int _numElements;
	};



	virtual HdBufferArraySharedPtr CreateBufferArray(
		TfToken const &role,
		HdBufferSpecVector const &bufferSpecs) override
	{
		return boost::make_shared<HdRprStrategy::_BufferArray>(
			role, bufferSpecs);
	}

	virtual HdBufferArrayRangeSharedPtr CreateBufferArrayRange() override
	{
		return HdBufferArrayRangeSharedPtr(new _BufferArrayRange());
	}

	virtual AggregationId ComputeAggregationId(
		HdBufferSpecVector const &bufferSpecs) const override
	{
		// Always returns different value
		static std::atomic_uint id(0);
		AggregationId hash = id++;  // Atomic
		return hash;
	}

	virtual HdBufferSpecVector GetBufferSpecs(
		HdBufferArraySharedPtr const &bufferArray) const override
	{
		const auto ba =
			boost::static_pointer_cast<_BufferArray>(bufferArray);
		return ba->GetBufferSpecs();
	}

	virtual size_t GetResourceAllocation(
		HdBufferArraySharedPtr const &bufferArray,
		VtDictionary &result) const override
	{
		return 0;
	}
};


HdRprResourceRegistry::HdRprResourceRegistry()
{	
	this->SetNonUniformAggregationStrategy(new HdRprStrategy);
	this->SetNonUniformImmutableAggregationStrategy(new HdRprStrategy);
	this->SetUniformAggregationStrategy(new HdRprStrategy);
	this->SetShaderStorageAggregationStrategy(new HdRprStrategy);
	this->SetSingleStorageAggregationStrategy(new HdRprStrategy);
}

HdRprResourceRegistry::~HdRprResourceRegistry()
{
}

void HdRprResourceRegistry::_GarbageCollect()
{
}

void HdRprResourceRegistry::_TallyResourceAllocation(VtDictionary * result) const
{
}

PXR_NAMESPACE_CLOSE_SCOPE*/